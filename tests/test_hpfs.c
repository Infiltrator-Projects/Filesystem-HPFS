/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <infiltratr/filesystem_module_v2.h>
#include "../Libraries/tests/filesystem_allocation_pages.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum{N=256,B=512};
static uint8_t disk[N*B],saved[N*B];static unsigned sector,allocs,fail;
static uint8_t *blk(unsigned b){return disk+B*b;}
static void w16(uint8_t *p,unsigned n){p[0]=n;p[1]=n>>8;}
static void w32(uint8_t *p,uint32_t n){w16(p,n);w16(p+2,n>>16);}
static void take(unsigned b,unsigned n){for(unsigned i=b;i<b+n;++i)blk(20)[i/8]&=~(1u<<(i%8));}
static void fn(unsigned id,unsigned parent,unsigned dir,unsigned bytes)
{
    uint8_t *p=blk(id);take(id,1);w32(p,0xf7e40aae);w32(p+28,parent);w32(p+160,bytes);
    if(dir){w16(p+54,256);p[61]=1;p[60]=7;w16(p+62,20);w32(p+68,4);w32(p+72,dir);}
}
static unsigned de(uint8_t *p,unsigned id,unsigned flags,unsigned attr,const char *name,unsigned n,unsigned size,unsigned down)
{
    unsigned len=((31+n+3)&~3u)+(down?4:0);w16(p,len);p[2]=flags|(down?4:0);p[3]=attr;
    w32(p+4,id);w32(p+12,size);p[30]=n;memcpy(p+31,name,n);if(down)w32(p+len-4,down);return len;
}
static uint8_t *dn(unsigned id,unsigned parent,unsigned root)
{
    uint8_t *p=blk(id);w32(p,0x77e40aae);w32(p+16,id);w32(p+12,parent);p[8]=root;
    blk(28)[(id-32)/32]&=~(1u<<(((id-32)/4)%8));return p;
}
static void fixture(void)
{
    memset(disk,0,sizeof disk);memset(blk(20),255,2048);memset(blk(28),255,2048);
    take(0,64);w32(blk(24),20);
    uint8_t *p=blk(16);w32(p,0xf995e849);w32(p+4,0xfa53e9c5);p[8]=2;p[9]=2;
    w32(p+12,64);w32(p+16,N);w32(p+24,24);w32(p+44,32);w32(p+48,32);w32(p+52,63);w32(p+56,28);
    p=blk(17);w32(p,0xf9911849);w32(p+4,0xfa5229c5);
    fn(64,64,32,0);fn(65,64,0,700);fn(66,64,40,0);fn(67,66,0,3);fn(68,64,0,1);
    p=blk(65);p[56]=128;p[60]=11;p[61]=1;w16(p+62,16);w32(p+64,UINT32_MAX);w32(p+68,70);
    take(70,1);p=blk(70);w32(p,0x37e40aae);w32(p+4,70);w32(p+8,65);p[12]=32;p[16]=38;p[17]=2;w16(p+18,32);
    w32(p+20,0);w32(p+24,1);w32(p+28,80);w32(p+32,1);w32(p+36,1);w32(p+40,90);
    take(80,1);take(90,1);
    p=blk(67);p[60]=7;p[61]=1;w16(p+62,20);w32(p+68,1);w32(p+72,81);take(81,1);memcpy(blk(81),"yes",3);
    p=blk(68);p[60]=7;p[61]=1;w16(p+62,20);w32(p+68,1);w32(p+72,82);take(82,1);blk(82)[0]='X';
    p=dn(32,64,1);unsigned pos=20;
    pos+=de(p+pos,64,1,16,"\1\1",2,0,0);
    pos+=de(p+pos,66,0,16,"dir",3,0,36);
    pos+=de(p+pos,0,8,0,"\377",1,0,0);w32(p+4,pos);
    p=dn(36,32,0);pos=20;
    pos+=de(p+pos,65,0,0,"boot.efi",8,700,0);pos+=de(p+pos,68,0,0,"c",1,1,0);
    pos+=de(p+pos,0,8,0,"\377",1,0,0);w32(p+4,pos);
    p=dn(40,66,1);pos=20;pos+=de(p+pos,66,1,16,"\1\1",2,0,0);
    pos+=de(p+pos,67,0,0,"child",5,3,0);pos+=de(p+pos,0,8,0,"\377",1,0,0);w32(p+4,pos);
    for(unsigned i=0;i<512;++i){blk(80)[i]=(uint8_t)(i*31+2);blk(90)[i]=(uint8_t)(i*17+3);}
    memcpy(saved,disk,sizeof disk);
}
static int rd(void *c,uint64_t off,void *out,uint32_t n)
{(void)c;if(fail||off>sizeof disk||n>sizeof disk-off||off%sector||n%sector)return -1;memcpy(out,disk+off,n);return 0;}
static void *alloc(void *c,size_t n){(void)c;void *p=malloc(n);if(p)++allocs;return p;}
static void drop(void *c,void *p){(void)c;if(p){assert(allocs);--allocs;free(p);}}
static int fixture_allocated(uint64_t block)
{
    return !(blk(20)[block/8] & (1u<<(block%8)));
}
int main(int argc,char **argv)
{
    fixture();
    if(argc==4&&!strcmp(argv[1],"--emit")){
        FILE *img=fopen(argv[2],"wb"),*proof=fopen(argv[3],"wb");assert(img&&proof);
        assert(fwrite(disk,1,sizeof disk,img)==sizeof disk);
        assert(fwrite(blk(80),1,512,proof)==512&&fwrite(blk(90),1,188,proof)==188);
        assert(!fclose(img)&&!fclose(proof));return 0;
    }
    assert(argc==2);void *lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
    IfsFsModuleApiEntryV2 entry;*(void **)(&entry)=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);assert(entry);
    const IfsFsModuleApiV2 *api=entry();assert(ifs_fs_api_v2_valid(api,"hpfs"));
    const IfsFsCoreOpsV2 *core=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);assert(core&&!core->write);
    for(sector=512;sector<=4096;sector*=8){
        IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof(h),.capabilities=IFS_FS_HOST_READ_ONLY,
          .volume_bytes=sizeof disk,.logical_sector_bytes=sector,.physical_sector_bytes=sector,.maximum_transfer_bytes=4096,
          .read_at=rd,.allocate=alloc,.release=drop};
        uint32_t got=0;assert(core->probe(&h,&got)==0&&got==1&&!allocs);
        void *v=NULL,*other=NULL;IfsFsVolumeInfoV2 i={.structure_bytes=sizeof i};
        assert(core->mount(&h,&v,&i)==0&&v&&i.total_bytes==sizeof disk&&i.free_bytes==(N-74)*B);
        const IfsFsAllocationOpsV2 *allocation=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_ALLOCATION);
        check_allocation_pages(allocation,v,B,h.volume_bytes,i.free_bytes,fixture_allocated);
        assert(core->mount(&h,&other,&i)==0&&other!=v);
        char name[256];IfsFsEntryV2 e={.structure_bytes=sizeof e,.name=name,.name_capacity=sizeof name};
        assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,"boot.efi"));
        assert(core->directory_entry(v,"/",1,&e)==0&&!strcmp(name,"c"));
        assert(core->directory_entry(v,"/",2,&e)==0&&!strcmp(name,"dir"));
        assert(core->directory_entry(v,"/",3,&e)==IFS_FS_END);
        assert(core->stat(v,"/BOOT.EFI",&e)==0&&e.size_bytes==700);
        uint8_t buf[700];assert(core->read(v,"/boot.efi",0,buf,700)==0&&!memcmp(buf,blk(80),512)&&!memcmp(buf+512,blk(90),188));
        assert(core->read(v,"/dir/child",0,buf,3)==0&&!memcmp(buf,"yes",3));
        w32(blk(70)+40,70);assert(core->read(v,"/boot.efi",512,buf,1)==IFS_FS_CORRUPT);
        memcpy(disk,saved,sizeof disk);
        w32(blk(70)+40,91);assert(core->read(v,"/boot.efi",512,buf,1)==IFS_FS_CORRUPT);
        memcpy(disk,saved,sizeof disk);
        w32(blk(36)+12,40);assert(core->stat(v,"/boot.efi",&e)==IFS_FS_CORRUPT);
        memcpy(disk,saved,sizeof disk);
        fail=1;assert(core->stat(v,"/",&e)!=0);fail=0;
        assert(core->unmount(v)==0&&core->unmount(v)==IFS_FS_INVALID&&core->unmount(other)==0&&!allocs);
        assert(!memcmp(disk,saved,sizeof disk));
    }
    puts("HPFS bitmap, internal allocation and directory trees, fragmented reads, corrupt pointers PASS");dlclose(lib);return 0;
}
