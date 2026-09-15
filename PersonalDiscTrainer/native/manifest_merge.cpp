// Merge only explicitly hash-pinned tablet resources into the manifest.
// Output goes to a staging directory; install.ps1 owns backup/commit/restore.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <vector>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <string>
#include "vendor/zstd/lib/zstd.h"
using U=uint64_t; using Bytes=std::vector<unsigned char>;
static Bytes read(const std::filesystem::path& p) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);if(!f) throw std::runtime_error("Cannot read input");
    auto n=f.tellg();if(n<0 || n>128*1024*1024)throw std::runtime_error("Input size invalid");
    Bytes b((size_t)n);f.seekg(0);f.read((char*)b.data(),n);if(!f)throw std::runtime_error("Short read");return b;
}
static void write(const std::filesystem::path& p,const Bytes& b) {
    std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write((const char*)b.data(),b.size());if(!f)throw std::runtime_error("Output write failed");
}
template<class T> static T get(const Bytes& b,size_t p) {
    if(p+sizeof(T)>b.size())throw std::runtime_error("Truncated input");T v;memcpy(&v,b.data()+p,sizeof(v));return v;
}
template<class T> static void put(Bytes& b,size_t p,T v) {if(p+sizeof(T)>b.size())throw std::runtime_error("Output bounds");memcpy(b.data()+p,&v,sizeof(v));}
template<class T> static void append(Bytes& b,T v) {size_t n=b.size();b.resize(n+sizeof(v));put(b,n,v);}
struct A {U type,name,loc; uint32_t size,align;};
struct B {U type,name,lo,hi,stamp;};
struct C {uint32_t pkg,offset,csize,usize;};
struct Entry {A a; B b;};
static_assert(sizeof(A)==32 && sizeof(B)==40 && sizeof(C)==16);
static Bytes unpack(const Bytes& raw) {
    if(raw.size()<24 || memcmp(raw.data(),"ZSTD",4))throw std::runtime_error("Unexpected manifest container");
    U size=get<U>(raw,8), compressed=get<U>(raw,16);
    if(size>128*1024*1024 || compressed!=raw.size()-24)throw std::runtime_error("Invalid manifest lengths");
    Bytes b((size_t)size);auto n=ZSTD_decompress(b.data(),b.size(),raw.data()+24,(size_t)compressed);
    if(ZSTD_isError(n) || n!=size)throw std::runtime_error("Manifest decompression failed");return b;
}
static Bytes pack(const Bytes& b) {
    Bytes raw(24+ZSTD_compressBound(b.size()));memcpy(raw.data(),"ZSTD",4);put<uint32_t>(raw,4,16);put<U>(raw,8,b.size());
    auto n=ZSTD_compress(raw.data()+24,raw.size()-24,b.data(),b.size(),3);if(ZSTD_isError(n))throw std::runtime_error("Compression failed");
    put<U>(raw,16,n);raw.resize(n+24);return raw;
}
static void descriptor(Bytes& out,size_t off,U count,U stride) {
    put<U>(out,off+8,count*stride);put<uint32_t>(out,off+28,1);put<U>(out,off+32,32);put<U>(out,off+40,count);put<U>(out,off+48,count);
}
int wmain(int argc,wchar_t** argv) {
    try {
        if(argc!=4)throw std::runtime_error("Usage: manifest_merge manifest tablet.patch staging-directory");
        auto original=read(argv[1]), blob=unpack(original), patches=read(argv[2]);
        U na=get<U>(blob,8+48),nb=get<U>(blob,0x48+48),nc=get<U>(blob,0x88+48);
        if(na!=nb || na>1000000 || nc>1000000 || 192+na*72+nc*16!=blob.size())throw std::runtime_error("Invalid manifest tables");
        std::vector<Entry> entries;for(U i=0;i<na;i++) {
            auto a=get<A>(blob,192+i*32);auto b=get<B>(blob,192+na*32+i*40);
            if(a.type!=b.type || a.name!=b.name)throw std::runtime_error("Manifest A/B mismatch");entries.push_back({a,b});
        }
        std::vector<C> frames;for(U i=0;i<nc;i++)frames.push_back(get<C>(blob,192+na*72+i*16));
        if(patches.size()<8)throw std::runtime_error("Invalid tablet patch");
        bool v2=memcmp(patches.data(),"TTP2",4)==0;
        unsigned count=get<uint32_t>(patches,4);
        if((!v2 && memcmp(patches.data(),"TTP1",4)) || !count || count>16)throw std::runtime_error("Invalid tablet patch");
        uint32_t pkg=get<uint32_t>(blob,0);if(pkg>100000)throw std::runtime_error("Invalid package count");
        size_t cursor=8;Bytes data;unsigned changes=0;
        for(unsigned i=0;i<count;i++) {
            unsigned allowed=1;if(v2) {allowed=get<uint32_t>(patches,cursor);cursor+=4;}
            if(!allowed || allowed>8)throw std::runtime_error("Invalid baseline count");
            std::vector<std::array<U,2>> baselines;
            for(unsigned j=0;j<allowed;j++) {baselines.push_back({get<U>(patches,cursor),get<U>(patches,cursor+8)});cursor+=16;}
            A a=get<A>(patches,cursor);cursor+=32;B b=get<B>(patches,cursor);cursor+=40;
            uint32_t len=get<uint32_t>(patches,cursor);cursor+=4;
            if(cursor+len>patches.size() || len>8*1024*1024 || a.type!=b.type || a.name!=b.name)throw std::runtime_error("Invalid resource patch");
            auto it=std::find_if(entries.begin(),entries.end(),[&](auto& e){return e.a.type==a.type && e.a.name==a.name;});
            bool same=it!=entries.end() && it->b.lo==b.lo && it->b.hi==b.hi;
            std::array<U,2> existing=it==entries.end()?std::array<U,2>{0,0}:std::array<U,2>{it->b.lo,it->b.hi};
            if(!same && std::find(baselines.begin(),baselines.end(),existing)==baselines.end())
                throw std::runtime_error("A tablet resource has another modification; refusing to overwrite it");
            if(!same) changes++;
            a.loc=frames.size();
            frames.push_back({pkg,(uint32_t)data.size(),len,a.size});
            data.insert(data.end(),patches.begin()+cursor,patches.begin()+cursor+len);cursor+=len;
            if(it==entries.end())entries.push_back({a,b});else *it={a,b};
        }
        if(cursor!=patches.size())throw std::runtime_error("Unexpected trailing patch data");
        std::filesystem::path out(argv[3]);std::filesystem::create_directories(out);
        if(!changes) {write(out/L"manifest",original);std::puts("unchanged");return 0;}
        frames.push_back({pkg,(uint32_t)data.size(),0,0});
        std::sort(entries.begin(),entries.end(),[](auto& a,auto& b){return std::array<int64_t,2>{(int64_t)a.a.type,(int64_t)a.a.name}<std::array<int64_t,2>{(int64_t)b.a.type,(int64_t)b.a.name};});
        Bytes next(192,0);put<uint32_t>(next,0,pkg+1);put<uint32_t>(next,4,get<uint32_t>(blob,4));
        descriptor(next,8,entries.size(),32);descriptor(next,0x48,entries.size(),40);descriptor(next,0x88,frames.size(),16);
        for(auto e:entries)append(next,e.a);for(auto e:entries)append(next,e.b);for(auto f:frames)append(next,f);
        write(out/L"manifest",pack(next));write(out/(L"48037dc70b0ecab2_"+std::to_wstring(pkg)),data);
        printf("merged %u tablet resources; package %u\n",changes,pkg);
        return 0;
    } catch(const std::exception& e) {fprintf(stderr,"%s\n",e.what());return 1;}
}
