from building import *

src	= Glob('*.c')
cwd = GetCurrentDir()

if GetDepend('RT_USING_UTEST'):
    src += ['testcases/cmsis_rtos_tc.c']

path = [cwd]
group = DefineGroup('CMSIS-RTOS1', src, depend = ['PKG_USING_CMSIS_RTOS1'], CPPPATH = path)
Return('group')
