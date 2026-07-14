import glob
from pathlib import Path
import subprocess

for filename in glob.iglob("shader/*.slang"):
    stages = []
    with open(filename) as f:
        content = f.read()
        if '[shader("vertex")]' in content: stages.append("vertex")
        if '[shader("fragment")]' in content: stages.append("fragment")
        if '[shader("compute")]' in content: stages.append("compute")
    
    for stage in stages:
        ext = ""
        match stage:
            case "vertex": ext = ".vert"
            case "fragment": ext = ".frag"
            case "compute": ext = ".comp"
            case _: assert(False)

        out_file = Path(filename).with_suffix(f'{ext}.spv')
        subprocess.call(f"slangc {filename} -profile spirv_1_4 -matrix-layout-column-major -target spirv -o {out_file} -entry {stage}_main -stage {stage}", shell=True)
