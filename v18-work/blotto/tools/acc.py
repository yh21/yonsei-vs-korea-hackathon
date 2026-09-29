import re,sys
tw=hw=tf=hf=0
for l in open(sys.argv[1]):
    m=re.match(r'T(\d+) predW (\d+)/(\d+) predF (\d+)/(\d+)',l.strip())
    if not m: continue
    a=list(map(int,m.groups())); hw+=a[1]; tw+=a[2]; hf+=a[3]; tf+=a[4]
print('W acc %.3f F acc %.3f'%(hw/max(1,tw),hf/max(1,tf)))
