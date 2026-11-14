import sys
from PIL import Image
ims=[Image.open(p).convert('RGB') for p in sys.argv[2:]]
s=3
W=sum(i.width*s+8 for i in ims)
out=Image.new('RGB',(W,240*s),(255,255,255))
x=0
for i in ims:
    out.paste(i.resize((i.width*s,i.height*s),Image.NEAREST),(x,0)); x+=i.width*s+8
out.save(sys.argv[1])
