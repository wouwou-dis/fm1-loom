import sys
from PIL import Image
out=sys.argv[1]; files=sys.argv[2:]; sc=2; cols=min(3,len(files))
rows=(len(files)+cols-1)//cols
g=Image.new('RGB',(cols*(240*sc+8),rows*(240*sc+8)),'white')
for i,f in enumerate(files):
    im=Image.open(f).convert('RGB').resize((240*sc,240*sc),Image.NEAREST)
    g.paste(im,((i%cols)*(240*sc+8),(i//cols)*(240*sc+8)))
g.save(out)
