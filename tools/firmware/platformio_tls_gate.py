"""PlatformIO post-link gate for firmware using FreeRTOS RISC-V task locals."""
import importlib.util

Import('env')
spec = importlib.util.spec_from_file_location(
    'freeink_tls_gate', env.File('check_tls_layout.py').srcnode().get_abspath())
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


def verify(source, target, env):
    print('FreeRTOS TLS layout verified:', gate.check(str(target[0])))


env.AddPostAction('$BUILD_DIR/${PROGNAME}.elf', verify)
