import sqlite3, struct, random, os
random.seed(3)
if os.path.exists('content.db'): os.remove('content.db')
c=sqlite3.connect('content.db'); c.execute('PRAGMA page_size=1024')
c.execute('CREATE TABLE "ContentItems" ("Id" INTEGER PRIMARY KEY, "Directory" TEXT, "Executable" TEXT, "TitleId" INTEGER, "MediaId" INTEGER, "TitleName" TEXT, "Description" TEXT)')
c.execute('CREATE TABLE Other (TitleName TEXT, TitleId INTEGER)')
c.execute("INSERT INTO Other VALUES ('WRONG', 1)")
rows=[]
for i in range(1500):
    tid=0x41560000+i
    name=("Jeu numéro %d — édition spéciale "%i)+("x"*random.randint(0,40))
    desc="D"*random.choice([10,3000,9000])
    rows.append((tid,name))
    c.execute('INSERT INTO ContentItems (Directory,Executable,TitleId,MediaId,TitleName,Description) VALUES (?,?,?,?,?,?)',("Hdd1:\\Games\\%d"%i,"default.xex",tid,random.randint(0,2**31),name,desc))
    if i%7==0: c.execute('INSERT INTO ContentItems (Directory,Executable,TitleId,MediaId,TitleName,Description) VALUES (?,?,?,?,?,?)',("disc2","default.xex",tid,1,name,"x"))
c.commit(); c.close()
open('expected.txt','w',encoding='utf-8').write("\n".join("%08X\t%s"%r for r in rows))

# ---- asset ----
from PIL import Image
W,H=90,123
img=Image.new('RGBA',(W,H))
px=img.load()
for y in range(H):
    for x in range(W): px[x,y]=((x*255)//W,(y*255)//H,128,255 if (x+y)%17 else 200)
def asset(fmt):
    pw=32*((W+31)//32); ph=32*((H+31)//32)
    pad=Image.new('RGBA',(pw,ph)); pad.paste(img,(0,0))
    raw=pad.tobytes()
    if fmt==6:
        data=bytearray(raw)
    else:
        # encodeur BC3 simpliste : couleurs extremes du bloc, alpha idem
        data=bytearray()
        for by in range(0,ph,4):
            for bx in range(0,pw,4):
                blk=[pad.getpixel((bx+i%4,by+i//4)) for i in range(16)]
                a0=max(p[3] for p in blk); a1=min(p[3] for p in blk)
                if a0==a1: a1=max(0,a0-1)
                ai=[]
                for p in blk:
                    # palette 8 niveaux (a0>a1)
                    pal=[a0,a1]+[((7-k)*a0+k*a1)//7 for k in range(1,7)]
                    ai.append(min(range(8),key=lambda j:abs(pal[j]-p[3])))
                bits=0
                for i,v in enumerate(ai): bits|=v<<(3*i)
                def to565(p): return ((p[0]*31//255)<<11)|((p[1]*63//255)<<5)|(p[2]*31//255)
                cs=sorted(blk,key=lambda p:p[0]+p[1]+p[2])
                c0=to565(cs[-1]); c1=to565(cs[0])
                if c0<c1: c0,c1=c1,c0
                idx=0
                def exp(c): return (((c>>11)&31)*255//31,((c>>5)&63)*255//63,(c&31)*255//31)
                e0,e1=exp(c0),exp(c1)
                pal=[e0,e1,tuple((2*a+b)//3 for a,b in zip(e0,e1)),tuple((a+2*b)//3 for a,b in zip(e0,e1))]
                for i,p in enumerate(blk):
                    j=min(range(4),key=lambda j:sum((pal[j][k]-p[k])**2 for k in range(3)))
                    idx|=j<<(2*i)
                data+=bytes([a0,a1])+bits.to_bytes(6,'little')+struct.pack('<HHI',c0,c1,idx)
    # endian 8in16
    for k in range(0,len(data),2): data[k],data[k+1]=data[k+1],data[k]
    hdr=bytearray(0x800)
    struct.pack_into('>IIIII',hdr,0,0x52584541,1,len(data),1<<2,0)
    e=0x14+2*0x40
    c0=((W+31)//32)<<22
    c1=(1<<6)|fmt
    c2=((H-1)<<13)|(W-1)
    c3=(0<<1)|(1<<4)|(2<<7)|(3<<10)
    c5=(1<<9)
    struct.pack_into('>III',hdr,e,0,len(data),0)
    struct.pack_into('>IIIIII',hdr,e+0xC+0x1C,c0,c1,c2,c3,0,c5)
    return bytes(hdr)+bytes(data)
open('rgba.asset','wb').write(asset(6)); open('bc3.asset','wb').write(asset(20))
img.save('ref.png')
print("ok", os.path.getsize('content.db'))
