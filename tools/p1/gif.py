import sys, glob
from PIL import Image
out, pat = sys.argv[1], sys.argv[2]
fr = [Image.open(f).convert('RGB').resize((480, 480), Image.NEAREST) for f in sorted(glob.glob(pat))]
fr = [f.quantize(colors=128, method=Image.MEDIANCUT) for f in fr]
fr[0].save(out, save_all=True, append_images=fr[1:], duration=80, loop=0, optimize=False)
print(out, len(fr))
