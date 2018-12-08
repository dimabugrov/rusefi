rem 
rem STM32F767 version of the firmware for Nucleo-F767ZI board
rem

cd ../../..
set PROJECT_BOARD=NUCLEO_F767
set PROJECT_CPU=ST_STM32F7
set EXTRA_PARAMS="-DDUMMY -DSTM32F767xx -DEFI_ENABLE_ASSERTS=FALSE -DCH_DBG_ENABLE_CHECKS=FALSE -DCH_DBG_ENABLE_TRACE=FALSE -DCH_DBG_ENABLE_ASSERTS=FALSE -DCH_DBG_ENABLE_STACK_CHECK=FALSE -DCH_DBG_FILL_THREADS=FALSE -DCH_DBG_THREADS_PROFILING=FALSE"
set DEBUG_LEVEL_OPT="-O2"
call compile_and_program.bat -r