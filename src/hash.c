#include "hash.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const uint32_t K[64] = {
    0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
    0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
    0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
    0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
    0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
    0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
    0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
    0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
static const int R[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,
                          5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                          4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,
                          6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
static void block(uint32_t h[4], const unsigned char *b) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++)
        w[i] = b[i*4] | b[i*4+1]<<8 | b[i*4+2]<<16 | (uint32_t)b[i*4+3]<<24;
    uint32_t a=h[0],bb=h[1],c=h[2],d=h[3];
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i<16){f=(bb&c)|(~bb&d);g=i;}
        else if(i<32){f=(d&bb)|(~d&c);g=(5*i+1)%16;}
        else if(i<48){f=bb^c^d;g=(3*i+5)%16;}
        else{f=c^(bb|~d);g=(7*i)%16;}
        uint32_t t=d; d=c; c=bb;
        uint32_t x=a+f+K[i]+w[g];
        bb=bb+(x<<R[i]|x>>(32-R[i]));
        a=t;
    }
    h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;
}
void md5_hex(const void *data, size_t len, char out[33]) {
    uint32_t h[4]={0x67452301,0xefcdab89,0x98badcfe,0x10325476};
    const unsigned char *p=data; size_t i=0;
    for (;i+64<=len;i+=64) block(h,p+i);
    unsigned char tail[128]={0};
    size_t rest=len-i; memcpy(tail,p+i,rest);
    tail[rest]=0x80; size_t tl=rest<56?64:128;
    uint64_t bits=(uint64_t)len*8;
    for(int k=0;k<8;k++) tail[tl-8+k]=(unsigned char)(bits>>(8*k));
    block(h,tail); if(tl==128) block(h,tail+64);
    for(int k=0;k<16;k++) sprintf(out+k*2,"%02x",(h[k/4]>>(8*(k%4)))&255);
}
static uint32_t rol(uint32_t x,int n){return x<<n|x>>(32-n);}
static void sha1_block(uint32_t h[5], const unsigned char *b) {
    uint32_t w[80];
    for(int i=0;i<16;i++) w[i]=(uint32_t)b[i*4]<<24|b[i*4+1]<<16|b[i*4+2]<<8|b[i*4+3];
    for(int i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=h[0],bb=h[1],c=h[2],d=h[3],e=h[4];
    for(int i=0;i<80;i++){
        uint32_t f,k;
        if(i<20){f=(bb&c)|(~bb&d);k=0x5a827999;}
        else if(i<40){f=bb^c^d;k=0x6ed9eba1;}
        else if(i<60){f=(bb&c)|(bb&d)|(c&d);k=0x8f1bbcdc;}
        else{f=bb^c^d;k=0xca62c1d6;}
        uint32_t t=rol(a,5)+f+e+k+w[i];
        e=d;d=c;c=rol(bb,30);bb=a;a=t;
    }
    h[0]+=a;h[1]+=bb;h[2]+=c;h[3]+=d;h[4]+=e;
}
void sha1_hex(const void *data, size_t len, char out[41]) {
    uint32_t h[5]={0x67452301,0xefcdab89,0x98badcfe,0x10325476,0xc3d2e1f0};
    const unsigned char *p=data; size_t i=0;
    for (;i+64<=len;i+=64) sha1_block(h,p+i);
    unsigned char tail[128]={0};
    size_t rest=len-i; memcpy(tail,p+i,rest);
    tail[rest]=0x80; size_t tl=rest<56?64:128;
    uint64_t bits=(uint64_t)len*8;
    for(int k=0;k<8;k++) tail[tl-1-k]=(unsigned char)(bits>>(8*k));
    sha1_block(h,tail); if(tl==128) sha1_block(h,tail+64);
    for(int k=0;k<5;k++) sprintf(out+k*8,"%08x",h[k]);
}
