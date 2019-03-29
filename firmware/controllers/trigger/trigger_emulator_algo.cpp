/**
 * @file trigger_emulator_algo.cpp
 *
 * This file is about producing real electrical signals which emulate trigger signal based on
 * a known TriggerShape.
 *
 * Historically this implementation was implemented based on PwmConfig which is maybe not the
 * best way to implement it. (todo: why is not the best way?)
 *
 * A newer implementation of pretty much the same thing is TriggerStimulatorHelper
 * todo: one emulator should be enough! another one should be eliminated
 *
 * @date Mar 3, 2014
 * @author Andrey Belomutskiy, (c) 2012-2018
 */
#include "global.h"
#include "efi_wave.h"

int getPreviousIndex(const int currentIndex, const int size) {
	return (currentIndex + size - 1) % size;
}

bool needEvent(const int currentIndex, const int size, MultiWave *multiWave, int channelIndex) {
	int prevIndex = getPreviousIndex(currentIndex, size);
	pin_state_t previousValue = multiWave->getChannelState(channelIndex, /*phaseIndex*/prevIndex);
	pin_state_t currentValue = multiWave->getChannelState(channelIndex, /*phaseIndex*/currentIndex);
	return previousValue != currentValue;
}

#if EFI_EMULATE_POSITION_SENSORS || defined(__DOXYGEN__)

#include "engine.h"
#include "trigger_emulator_algo.h"
#include "engine_configuration.h"
#include "trigger_central.h"
#include "trigger_simulator.h"

#include "pwm_generator.h"

TriggerEmulatorHelper::TriggerEmulatorHelper() {
}

EXTERN_ENGINE
;

void TriggerEmulatorHelper::handleEmulatorCallback(PwmConfig *state, int stateIndex) {
	// todo: code duplication with TriggerStimulatorHelper::feedSimulatedEvent?
	MultiWave *multiWave = &state->multiWave;

	if (needEvent(stateIndex, state->phaseCount, &state->multiWave, 0)) {
		pin_state_t currentValue = multiWave->getChannelState(/*phaseIndex*/0, stateIndex);
		hwHandleShaftSignal(currentValue ? SHAFT_PRIMARY_RISING : SHAFT_PRIMARY_FALLING);
	}

	if (needEvent(stateIndex, state->phaseCount, &state->multiWave, 1)) {
		pin_state_t currentValue = multiWave->getChannelState(/*phaseIndex*/1, stateIndex);
		hwHandleShaftSignal(currentValue ? SHAFT_SECONDARY_RISING : SHAFT_SECONDARY_FALLING);
	}

	if (needEvent(stateIndex, state->phaseCount, &state->multiWave, 2)) {
		pin_state_t currentValue = multiWave->getChannelState(/*phaseIndex*/2, stateIndex);
		hwHandleShaftSignal(currentValue ? SHAFT_3RD_RISING : SHAFT_3RD_FALLING);
	}

	//	print("hello %d\r\n", chTimeNow());
}

/*
 * todo: should we simply re-use instances used by trigger_decoder?
 * todo: since we are emulating same shape we are decoding
 */
static pin_state_t pinStates1[PWM_PHASE_MAX_COUNT];
static pin_state_t pinStates2[PWM_PHASE_MAX_COUNT];
static pin_state_t pinStates3[PWM_PHASE_MAX_COUNT];
static SingleWave waves[PWM_PHASE_MAX_WAVE_PER_PWM] = { SingleWave(pinStates1), SingleWave(pinStates2),
		SingleWave(pinStates3) };
static SingleWave sr[PWM_PHASE_MAX_WAVE_PER_PWM] = { waves[0], waves[1], waves[2] };

static float switchTimesBuffer[PWM_PHASE_MAX_COUNT];

PwmConfig triggerSignal(switchTimesBuffer, sr);

#define DO_NOT_STOP 999999999

static int stopEmulationAtIndex = DO_NOT_STOP;
static bool isEmulating = true;

static Logging *logger;
static int atTriggerVersion = 0;

#if EFI_ENGINE_SNIFFER
#include "engine_sniffer.h"
extern WaveChart waveChart;
#endif /* EFI_ENGINE_SNIFFER */

void setTriggerEmulatorRPM(int rpm DECLARE_ENGINE_PARAMETER_SUFFIX) {
	engineConfiguration->bc.triggerSimulatorFrequency = rpm;
	/**
	 * All we need to do here is to change the periodMs
	 * togglePwmState() would see that the periodMs has changed and act accordingly
	 */
	if (rpm == 0) {
		triggerSignal.setFrequency(NAN);
	} else {
		float rpmM = getRpmMultiplier(engineConfiguration->operationMode);
		float rPerSecond = rpm * rpmM / 60.0; // per minute converted to per second
		triggerSignal.setFrequency(rPerSecond);
	}
#if EFI_ENGINE_SNIFFER
	if (engine->isTestMode)
		waveChart.reset();
#endif /* EFI_ENGINE_SNIFFER */

	scheduleMsg(logger, "Emulating position sensor(s). RPM=%d", rpm);
}

static void updateTriggerShapeIfNeeded(PwmConfig *state) {
	if (atTriggerVersion < engine->triggerCentral.triggerShape.version) {
		atTriggerVersion = engine->triggerCentral.triggerShape.version;
		scheduleMsg(logger, "Stimulator: updating trigger shape: %d/%d %d", atTriggerVersion,
				engine->getGlobalConfigurationVersion(), currentTimeMillis());


		TriggerShape *s = &engine->triggerCentral.triggerShape;
		pin_state_t *pinStates[PWM_PHASE_MAX_WAVE_PER_PWM] = {
				s->wave.channels[0].pinStates,
				s->wave.channels[1].pinStates,
				s->wave.channels[2].pinStates };
		copyPwmParameters(state, s->getSize(), s->wave.switchTimes, PWM_PHASE_MAX_WAVE_PER_PWM, pinStates);
		state->safe.periodNt = -1; // this would cause loop re-initialization
	}
}

static TriggerEmulatorHelper helper;

static void emulatorApplyPinState(PwmConfig *state, int stateIndex) {
	if (stopEmulationAtIndex == stateIndex) {
		isEmulating = false;
	}
	if (!isEmulating) {
		return;
	}
#if EFI_PROD_CODE || defined(__DOXYGEN__)
	applyPinState(state, stateIndex);
#endif /* EFI_PROD_CODE */
	if (engineConfiguration->directSelfStimulation) {
		/**
		 * this callback would invoke the input signal handlers directly
		 */
		helper.handleEmulatorCallback(state, stateIndex);
	}
}

static void setEmulatorAtIndex(int index) {
	stopEmulationAtIndex = index;
}

static void resumeStimulator() {
	isEmulating = true;
	stopEmulationAtIndex = DO_NOT_STOP;
}

void initTriggerEmulatorLogic(Logging *sharedLogger DECLARE_ENGINE_PARAMETER_SUFFIX) {
	logger = sharedLogger;

	TriggerShape *s = &engine->triggerCentral.triggerShape;
	setTriggerEmulatorRPM(engineConfiguration->bc.triggerSimulatorFrequency PASS_ENGINE_PARAMETER_SUFFIX);
	pin_state_t *pinStates[PWM_PHASE_MAX_WAVE_PER_PWM] = {
			s->wave.channels[0].pinStates,
			s->wave.channels[1].pinStates,
			s->wave.channels[2].pinStates };
	triggerSignal.weComplexInit("position sensor",
			&engine->executor,
			s->getSize(), s->wave.switchTimes, PWM_PHASE_MAX_WAVE_PER_PWM,
			pinStates, updateTriggerShapeIfNeeded, emulatorApplyPinState);

	addConsoleActionI("rpm", setTriggerEmulatorRPM);
	addConsoleActionI("stop_stimulator_at_index", setEmulatorAtIndex);
	addConsoleAction("resume_stimulator", resumeStimulator);
}
#endif /* EFI_EMULATE_POSITION_SENSORS */
