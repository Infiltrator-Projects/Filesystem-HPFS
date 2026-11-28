/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native HPFS reader: sector layout and tree definitions referenced from
 * Linux v6.12 fs/hpfs/hpfs.h, map.c and anode.c. New Common V2 engine,
 * no Linux VFS, kernel filesystem or external runtime. Initial profile:
 * clean standard HPFS, ASCII namespace, ordinary files without EA/ACL
 * semantics, no active hotfix remapping or HPFS386-specific features.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <infiltratr/filesystem_block_map_v2.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
enum { SECTOR=512,DNODE=2048,MAX_NAME=254,SLOTS=8,MAX_DEPTH=32,
       MAX_DNODES=65536,MAX_SECTORS=128*1024*1024 };
typedef struct {
    IfsFsHostRuntimeV2 host;
    uint32_t root,sectors,free_sectors,variant,band_start,band_count,
             dmap,bmdir,bmdir_count,bands;
    uint8_t *bits,*dirbits;uint32_t *maps;
    uintptr_t token;
} Volume;
typedef struct {uint32_t id,type,droot,bytes;uint8_t allocation[104];} Node;
typedef struct {Node node;uint32_t length;char name[MAX_NAME+1];} Entry;
static Volume slots[SLOTS];static uintptr_t generation;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static int enter(void){return !atomic_flag_test_and_set_explicit(&gate,memory_order_acquire);}
static int leave(int rc){atomic_flag_clear_explicit(&gate,memory_order_release);return rc;}
static void copy(void *d,const void *s,size_t n){uint8_t *o=d;const uint8_t *p=s;while(n--)*o++=*p++;}
static void zero(void *d,size_t n){uint8_t *p=d;while(n--)*p++=0;}
static uint32_t le16(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8;}
static uint32_t le32(const uint8_t *p){return le16(p)|le16(p+2)<<16;}
static int cancel(const Volume *v){return v->host.cancelled&&v->host.cancelled(v->host.context);}
static int rd(const Volume *v,uint64_t off,void *p,uint32_t n)
{return ifs_fs_host_read_bytes_v2(&v->host,off,p,n);}
static int span(const Volume *v,uint32_t at,uint32_t n)
{return n&&at<v->sectors&&n<=v->sectors-at;}
static int overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn)
{return a<(uint64_t)b+bn&&b<(uint64_t)a+an;}
static int bit(const uint8_t *p,uint32_t n){return (p[n/8]>>(n%8))&1;}
static int allocated(const Volume *v,uint32_t at,uint32_t n)
{
    if(!span(v,at,n))return 0;
    if(!v->bits)return 1;
    for(uint32_t j=0;j<n;++j)if(bit(v->bits,at+j))return 0;
    return 1;
}
static int system_span(const Volume *v,uint32_t at,uint32_t n)
{
    if(at<20||overlap(at,n,v->bmdir,v->bmdir_count)||overlap(at,n,v->dmap,4))return 1;
    for(uint32_t i=0;i<v->bands;++i)if(overlap(at,n,v->maps[i],4))return 1;
    return 0;
}
static int data_span(const Volume *v,uint32_t at,uint32_t n)
{return allocated(v,at,n)&&!system_span(v,at,n)&&!overlap(at,n,v->band_start,v->band_count);}
static void dispose(Volume *v)
{
    if(v->bits)v->host.release(v->host.context,v->bits);
    if(v->dirbits)v->host.release(v->host.context,v->dirbits);
    if(v->maps)v->host.release(v->host.context,v->maps);
    zero(v,sizeof(*v));
}
static int bitmap_load(Volume *v)
{
    v->bands=(v->sectors+16383)/16384;
    v->bmdir_count=(v->bands+127)/128;
    if(!span(v,v->bmdir,v->bmdir_count)||v->bmdir<20||
       !span(v,v->dmap,4)||v->dmap%4||v->dmap<20||
       overlap(v->bmdir,v->bmdir_count,v->dmap,4)||
       overlap(v->bmdir,v->bmdir_count,v->band_start,v->band_count)||
       overlap(v->dmap,4,v->band_start,v->band_count))return IFS_FS_CORRUPT;
    v->maps=v->host.allocate(v->host.context,(size_t)v->bands*4);
    v->bits=v->host.allocate(v->host.context,(size_t)v->bands*2048);
    v->dirbits=v->host.allocate(v->host.context,2048);
    if(!v->maps||!v->bits||!v->dirbits)return IFS_FS_NO_MEMORY;
    uint8_t p[4];
    for(uint32_t i=0;i<v->bands;++i){
        if(cancel(v))return IFS_FS_BUSY;
        int rc=rd(v,(uint64_t)v->bmdir*512+4*i,p,4);if(rc)return rc;
        uint32_t b=le32(p);
        if(!span(v,b,4)||b%4||b<20||overlap(b,4,v->bmdir,v->bmdir_count)||
           overlap(b,4,v->band_start,v->band_count)||overlap(b,4,v->dmap,4))return IFS_FS_CORRUPT;
        for(uint32_t j=0;j<i;++j)if(b==v->maps[j])return IFS_FS_CORRUPT;
        v->maps[i]=b;
        rc=rd(v,(uint64_t)b*512,v->bits+(size_t)i*2048,2048);if(rc)return rc;
    }
    int rc=rd(v,(uint64_t)v->dmap*512,v->dirbits,2048);if(rc)return rc;
    if(!allocated(v,0,20)||!allocated(v,v->bmdir,v->bmdir_count)||
       !allocated(v,v->dmap,4)||!allocated(v,v->band_start,v->band_count))return IFS_FS_CORRUPT;
    for(uint32_t i=0;i<v->bands;++i)if(!allocated(v,v->maps[i],4))return IFS_FS_CORRUPT;
    uint32_t count=0;
    for(uint32_t i=0;i<v->sectors;++i){
        if(!(i%16384)&&cancel(v))return IFS_FS_BUSY;
        count+=bit(v->bits,i);
    }
    v->free_sectors=count;return IFS_FS_OK;
}
static int fnode(const Volume *v,uint32_t id,uint32_t parent,Node *out)
{
    if(!data_span(v,id,1))return IFS_FS_CORRUPT;
    uint8_t raw[512];int rc=rd(v,(uint64_t)id*512,raw,512);if(rc)return rc;
    if(le32(raw)!=0xf7e40aaeu||(parent&&le32(raw+28)!=parent))return IFS_FS_CORRUPT;
    /* Do not reinterpret a Unix symlink or special file stored in EA
     * form as an ordinary boot payload. EA/ACL decoding is outstanding. */
    if(le32(raw+32)||le16(raw+40)||le32(raw+44)||le16(raw+52)||le32(raw+164))
        return IFS_FS_UNSUPPORTED;
    Node n={.id=id,.type=(le16(raw+54)&256)?IFS_FS_ENTRY_DIRECTORY:IFS_FS_ENTRY_FILE,
            .bytes=le32(raw+160)};
    copy(n.allocation,raw+56,104);
    if(n.type==IFS_FS_ENTRY_DIRECTORY){
        n.droot=le32(raw+72);
        if(!span(v,n.droot,4)||n.droot%4||raw[56]&128||raw[61]!=1||
           le32(raw+64)!=0||le32(raw+68)!=4)return IFS_FS_CORRUPT;
    }
    *out=n;return IFS_FS_OK;
}
static int valid_dnode(const Volume *v,uint32_t b)
{
    if(b%4||!allocated(v,b,4)||system_span(v,b,4))return 0;
    if(overlap(b,4,v->band_start,v->band_count)){
        if(b<v->band_start||b+4>v->band_start+v->band_count)return 0;
        return !bit(v->dirbits,(b-v->band_start)/4);
    }
    return 1;
}
typedef struct {
    const char *name;uint32_t length,index,parent,visited;
    Entry result;int found;
    uint8_t *seen,*frames;
} Search;
static unsigned fold(unsigned c){return c>='a'&&c<='z'?c-32:c;}
static int equal_name(const char *a,const char *b,uint32_t n)
{while(n--)if(fold((uint8_t)*a++)!=fold((uint8_t)*b++))return 0;return 1;}
static int walk(const Volume *v,uint32_t id,uint32_t up,unsigned depth,Search *s)
{
    if(cancel(v))return IFS_FS_BUSY;
    if(depth>=MAX_DEPTH||++s->visited>MAX_DNODES)return IFS_FS_UNSUPPORTED;
    if(!valid_dnode(v,id)||bit(s->seen,id/4))return IFS_FS_CORRUPT;
    s->seen[(id/4)/8]|=(uint8_t)(1u<<((id/4)%8));
    uint8_t *raw=s->frames+(size_t)depth*DNODE;int rc=rd(v,(uint64_t)id*512,raw,DNODE);if(rc)return rc;
    uint32_t end=le32(raw+4);
    if(le32(raw)!=0x77e40aaeu||le32(raw+16)!=id||le32(raw+12)!=up||
       !!(raw[8]&1)!=(depth==0)||end<52||end>DNODE||end%4)return IFS_FS_CORRUPT;
    int last=0;
    for(uint32_t pos=20;pos<end;){
        if(end-pos<32||last)return IFS_FS_CORRUPT;
        const uint8_t *p=raw+pos;uint32_t len=le16(p),flags=p[2],n=p[30];
        uint32_t min=((31+n+3)&~3u)+((flags&4)?4:0);
        if(len<32||len!=min||len>end-pos||!n||n>MAX_NAME)return IFS_FS_CORRUPT;
        if(flags&4){
            rc=walk(v,le32(p+len-4),id,depth+1,s);if(rc)return rc;
        }
        if(flags&8){
            if(n!=1||p[31]!=255||pos+len!=end)return IFS_FS_CORRUPT;
            last=1;
        }else if(flags&1){
            if(n!=2||p[31]!=1||p[32]!=1||le32(p+4)!=s->parent)return IFS_FS_CORRUPT;
        }else{
            char name[MAX_NAME+1];
            for(uint32_t i=0;i<n;++i){
                if(p[31+i]<32||p[31+i]>=127||p[31+i]=='/'||p[31+i]==92)
                    return IFS_FS_UNSUPPORTED;
                name[i]=(char)p[31+i];
            }
            name[n]=0;
            if(s->name?(s->length==n&&equal_name(name,s->name,n)):s->index==0){
                if(s->found&&s->name)return IFS_FS_CORRUPT;
                if(!s->found){
                    if(flags&0xf2||p[28]||le32(p+24))return IFS_FS_UNSUPPORTED;
                    Node node;rc=fnode(v,le32(p+4),s->parent,&node);if(rc)return rc;
                    if(node.id==s->parent||!!(p[3]&16)!=(node.type==IFS_FS_ENTRY_DIRECTORY)||
                       (node.type==IFS_FS_ENTRY_FILE&&node.bytes!=le32(p+12)))return IFS_FS_CORRUPT;
                    s->result.node=node;s->result.length=n;copy(s->result.name,name,n+1);s->found=1;
                }
            }
            if(!s->name&&s->index)--s->index;
        }
        pos+=len;
    }
    return last?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int child(const Volume *v,const Node *dir,const char *name,uint32_t length,uint32_t index,Entry *out)
{
    if(dir->type!=IFS_FS_ENTRY_DIRECTORY)return IFS_FS_NOT_FOUND;
    size_t bytes=((uint64_t)v->sectors+31)/32;
    uint8_t *seen=v->host.allocate(v->host.context,bytes);if(!seen)return IFS_FS_NO_MEMORY;
    uint8_t *frames=v->host.allocate(v->host.context,MAX_DEPTH*DNODE);
    if(!frames){v->host.release(v->host.context,seen);return IFS_FS_NO_MEMORY;}
    zero(seen,bytes);
    Search s={.name=name,.length=length,.index=index,.parent=dir->id,.seen=seen,.frames=frames};
    int rc=walk(v,dir->droot,dir->id,0,&s);
    v->host.release(v->host.context,frames);
    v->host.release(v->host.context,seen);
    if(rc)return rc;
    if(!s.found)return name?IFS_FS_NOT_FOUND:IFS_FS_END;
    *out=s.result;return IFS_FS_OK;
}
/* Allocation B+ tree. Validate every entry at the visited level, parent
 * links and bounds; never return zero-filled data for an unmapped sector. */
static int file_sector(const Volume *v,const Node *file,uint32_t logical,uint32_t *out)
{
    uint8_t raw[512];const uint8_t *p=file->allocation;
    uint32_t seen[MAX_DEPTH],parent=file->id;
    unsigned depth=0,first=1;uint64_t lower=0,upper=UINT64_C(1)<<32;
    for(;;){
        if(cancel(v))return IFS_FS_BUSY;
        unsigned internal=!!(p[0]&128),used=p[5],capacity=first?(internal?12:8):(internal?60:40);
        if(!used||used+p[4]!=capacity||le16(p+6)!=8+used*(internal?8:12))return IFS_FS_CORRUPT;
        if(internal){
            uint64_t previous=lower;uint32_t chosen=0;uint64_t lo=0,hi=0;
            for(unsigned i=0;i<used;++i){
                uint64_t limit=le32(p+8+i*8);uint32_t next=le32(p+12+i*8);
                if(limit==UINT32_MAX)limit=upper;
                if(limit<=previous||limit>upper||!data_span(v,next,1)||next==file->id)return IFS_FS_CORRUPT;
                if(logical>=previous&&logical<limit){chosen=next;lo=previous;hi=limit;}
                previous=limit;
            }
            if(!chosen||depth==MAX_DEPTH)return IFS_FS_CORRUPT;
            for(unsigned j=0;j<depth;++j)if(seen[j]==chosen)return IFS_FS_CORRUPT;
            seen[depth++]=chosen;
            int rc=rd(v,(uint64_t)chosen*512,raw,512);if(rc)return rc;
            if(le32(raw)!=0x37e40aaeu||le32(raw+4)!=chosen||le32(raw+8)!=parent||
               !!(raw[12]&32)!=first)return IFS_FS_CORRUPT;
            parent=chosen;first=0;p=raw+12;lower=lo;upper=hi;
        }else{
            uint64_t prev=lower;uint32_t result=0;
            for(unsigned i=0;i<used;++i){
                uint32_t start=le32(p+8+i*12),count=le32(p+12+i*12),at=le32(p+16+i*12);
                uint64_t end=(uint64_t)start+count;
                if(!count||start<prev||end>upper||!data_span(v,at,count)||
                   overlap(at,count,file->id,1))return IFS_FS_CORRUPT;
                for(unsigned j=0;j<depth;++j)if(overlap(at,count,seen[j],1))return IFS_FS_CORRUPT;
                for(unsigned j=0;j<i;++j)if(overlap(at,count,le32(p+16+j*12),le32(p+12+j*12)))return IFS_FS_CORRUPT;
                if(logical>=start&&logical<end)result=at+logical-start;
                prev=end;
            }
            if(!result)return IFS_FS_CORRUPT;
            *out=result;return IFS_FS_OK;
        }
    }
}
static int file_read(const Volume *v,const Node *n,uint64_t at,void *out,uint32_t size)
{
    if((!out&&size)||at>n->bytes||size>n->bytes-at)return IFS_FS_INVALID;
    uint8_t *p=out;
    while(size){
        uint32_t b,skip=(uint32_t)(at%512),chunk=512-skip;
        if(chunk>size)chunk=size;
        int rc=file_sector(v,n,(uint32_t)(at/512),&b);if(rc)return rc;
        rc=rd(v,(uint64_t)b*512+skip,p,chunk);if(rc)return rc;
        p+=chunk;at+=chunk;size-=chunk;
    }
    return IFS_FS_OK;
}
static int discover(const IfsFsHostRuntimeV2 *h,Volume *out)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    Volume v={0};size_t n=h->structure_bytes;if(n>sizeof(v.host))n=sizeof(v.host);
    copy(&v.host,h,n);v.host.structure_bytes=(uint32_t)n;
    if(h->volume_bytes<18*512)return IFS_FS_NOT_FOUND;
    uint8_t raw[512],spare[512];int rc=rd(&v,16*512,raw,512);if(rc)return rc;
    if(le32(raw)!=0xf995e849u||le32(raw+4)!=0xfa53e9c5u)return IFS_FS_NOT_FOUND;
    rc=rd(&v,17*512,spare,512);if(rc)return rc;
    if(le32(spare)!=0xf9911849u||le32(spare+4)!=0xfa5229c5u)return IFS_FS_CORRUPT;
    if(raw[8]!=2||raw[9]>2||(spare[8]&0x1f)||spare[9]||le32(spare+16)||
       le32(raw+20)||le32(spare+40)||le32(spare+44))return IFS_FS_UNSUPPORTED;
    v.variant=1;v.root=le32(raw+12);v.sectors=le32(raw+16);
    v.bmdir=le32(raw+24);v.band_count=le32(raw+44);v.band_start=le32(raw+48);v.dmap=le32(raw+56);
    if(v.sectors<32||v.sectors>MAX_SECTORS||v.sectors>h->volume_bytes/512||
       !span(&v,v.band_start,v.band_count)||v.band_start<20||v.band_start%4||
       v.band_count%4||v.band_count/4>16384||le32(raw+52)!=v.band_start+v.band_count-1)
        return IFS_FS_CORRUPT;
    rc=bitmap_load(&v);
    Node root;if(!rc)rc=fnode(&v,v.root,0,&root);
    if(!rc&&root.type!=IFS_FS_ENTRY_DIRECTORY)rc=IFS_FS_CORRUPT;
    if(!rc){Entry e;rc=child(&v,&root,NULL,0,UINT32_MAX,&e);if(rc==IFS_FS_END)rc=IFS_FS_OK;}
    if(rc){dispose(&v);return rc;}
    *out=v;return IFS_FS_OK;
}
static Volume *mounted(void *p)
{if(p)for(unsigned i=0;i<SLOTS;++i)if(slots[i].token==(uintptr_t)p)return &slots[i];return NULL;}
static void info(const Volume *v,IfsFsVolumeInfoV2 *o)
{*o=(IfsFsVolumeInfoV2){.structure_bytes=sizeof(*o),.variant=v->variant,.max_name_bytes=MAX_NAME,
.block_bytes=512,.total_bytes=(uint64_t)v->sectors*512,.free_bytes=(uint64_t)v->free_sectors*512};}
static int probe_impl(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{if(!variant)return IFS_FS_INVALID;Volume v={0};int rc=discover(h,&v);if(!rc)*variant=v.variant;dispose(&v);return rc;}
static int mount_impl(const IfsFsHostRuntimeV2 *h,void **out,IfsFsVolumeInfoV2 *o)
{
    if(!out||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    *out=NULL;if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(!(h->capabilities&IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    unsigned i=0;while(i<SLOTS&&slots[i].token)++i;
    if(i==SLOTS||generation==UINTPTR_MAX)return IFS_FS_BUSY;
    int rc=discover(h,&slots[i]);if(rc)return rc;
    slots[i].token=++generation;*out=(void *)slots[i].token;info(&slots[i],o);return IFS_FS_OK;
}
static int unmount_impl(void *p)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;dispose(v);return IFS_FS_OK;}
static int resolve(const Volume *v,const char *path,Entry *out)
{
    if(!path||*path!='/')return IFS_FS_INVALID;
    Entry e={.length=1,.name="/"};int rc=fnode(v,v->root,0,&e.node);if(rc)return rc;
    ++path;
    for(unsigned depth=0;*path;++depth){
        if(depth>=256)return IFS_FS_INVALID;
        uint32_t n=0;while(n<=MAX_NAME&&path[n]&&path[n]!='/')++n;
        if(!n||n>MAX_NAME||(n==1&&path[0]=='.')||(n==2&&equal_name(path,"..",2)))return IFS_FS_INVALID;
        Node dir=e.node;rc=child(v,&dir,path,n,0,&e);if(rc)return rc;
        path+=n;if(*path=='/'){++path;if(!*path)return IFS_FS_INVALID;}
    }
    *out=e;return IFS_FS_OK;
}
static int emit(const Entry *e,IfsFsEntryV2 *o)
{
    if(!o||o->structure_bytes<sizeof(*o)||!o->name)return IFS_FS_INVALID;
    if(o->name_capacity<=e->length)return IFS_FS_NO_SPACE;
    char *name=o->name;uint32_t cap=o->name_capacity;
    *o=(IfsFsEntryV2){.structure_bytes=sizeof(*o),.type=e->node.type,.size_bytes=e->node.bytes,
      .name=name,.name_capacity=cap,.name_bytes=e->length};
    copy(name,e->name,e->length+1);return IFS_FS_OK;
}
static int stat_impl(void *p,const char *path,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);return rc?rc:emit(&e,o);}
static int list_impl(void *p,const char *path,uint32_t i,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry dir,e;int rc=resolve(v,path,&dir);if(!rc)rc=child(v,&dir.node,NULL,0,i,&e);return rc?rc:emit(&e,o);}
static int read_impl(void *p,const char *path,uint64_t at,void *out,uint32_t n)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);if(rc)return rc;return e.node.type==IFS_FS_ENTRY_FILE?file_read(v,&e.node,at,out,n):IFS_FS_UNSUPPORTED;}
static int query_impl(void *p,IfsFsVolumeInfoV2 *o)
{Volume *v=mounted(p);if(!v||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;info(v,o);return IFS_FS_OK;}
#define WRAP(name,args,call) static int name args{if(!enter())return IFS_FS_BUSY;return leave(call);}
WRAP(probe,(const IfsFsHostRuntimeV2 *h,uint32_t *v),probe_impl(h,v))
WRAP(mount,(const IfsFsHostRuntimeV2 *h,void **p,IfsFsVolumeInfoV2 *o),mount_impl(h,p,o))
WRAP(unmount,(void *p),unmount_impl(p))
WRAP(stat_path,(void *p,const char *s,IfsFsEntryV2 *o),stat_impl(p,s,o))
WRAP(list_path,(void *p,const char *s,uint32_t i,IfsFsEntryV2 *o),list_impl(p,s,i,o))
WRAP(read_path,(void *p,const char *s,uint64_t a,void *b,uint32_t n),read_impl(p,s,a,b,n))
WRAP(query,(void *p,IfsFsVolumeInfoV2 *o),query_impl(p,o))
/* Mount has validated the native bitmap and reconciled its free count.
 * Use the same bit order and occupancy predicate as the namespace reader,
 * under the same mount lease, to export partition-relative byte ranges. */
static uint32_t allocation_state(const void *context, uint64_t block)
{
    const Volume *v = context;
    return allocated(v, (uint32_t)block, 1) ? IFS_FS_SPACE_ALLOCATED : IFS_FS_SPACE_FREE;
}

static int allocation_map(void *handle, uint64_t cursor, IfsFsAllocationRangeV2 *out,
                          uint32_t capacity, uint32_t *written)
{
    if (!written) return IFS_FS_INVALID;
    *written = 0;
    if (!enter()) return IFS_FS_BUSY;
    Volume *v = mounted(handle);
    if (!v) return leave(IFS_FS_INVALID);
    return leave(ifs_fs_block_map_page_v2(&v->host, 0, v->sectors, 512u, v,
                                           allocation_state, cursor, out, capacity, written));
}

static const IfsFsAllocationOpsV2 allocation_ops = {
    .header={2,sizeof(allocation_ops),IFS_FS_GROUP_ALLOCATION,0,IFS_FS_ALLOCATION_MAP},
    .map=allocation_map
};
static const IfsFsCoreOpsV2 core={.header={2,sizeof(core),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},.probe=probe,.mount=mount,.unmount=unmount,.stat=stat_path,.directory_entry=list_path,.read=read_path};
static const IfsFsVolumeOpsV2 volume={.header={2,sizeof(volume),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_QUERY},.query=query};
static const IfsFsGroupHeaderV2 *group(uint32_t id,uint32_t lo,uint32_t hi)
{if(lo>2||hi<2)return NULL;if(id==IFS_FS_GROUP_CORE)return &core.header;if(id==IFS_FS_GROUP_ALLOCATION)return &allocation_ops.header;
 if(id==IFS_FS_GROUP_VOLUME)return &volume.header;
 return NULL;}
static const IfsFsModuleApiV2 api={.abi_version=2,.structure_bytes=sizeof(api),.format_id="hpfs",
#ifdef IFS_BOOT_FIRMWARE
.runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
.runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
.group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_ALLOCATION)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME),.query_group=group};
__attribute__((visibility("default")))const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){return &api;}
