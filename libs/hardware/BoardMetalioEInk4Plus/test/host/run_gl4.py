from pathlib import Path
import subprocess,tempfile
here=Path(__file__).resolve().parent
sdk=here.parents[4]
with tempfile.TemporaryDirectory(prefix="pico-gl4-") as tmp:
 exe=Path(tmp)/"gl4"
 subprocess.run(["cc","-std=c11","-fsanitize=undefined","-I"+str(here.parents[1]/"src/waveform"),"-I"+str(sdk/"libs/display/epdiy/src"),str(here/"gl4.c"),"-o",str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print("GL48 source preservation, GL4 phases/targets and invalid-source rejection passed")
