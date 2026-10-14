typedef unsigned int u32;
typedef unsigned char u8;
extern u32 rxfilter_legacy_bitmap(const u8*);
extern u32 rxfilter_legacy_wrapper(const u8*, u32);
static u8 descriptor[112] __attribute__((aligned(4)));
static u8 station[16] __attribute__((aligned(4)));
static u8 bss[64] __attribute__((aligned(4)));
static u8 list[128] __attribute__((aligned(4)));
static u8 ethernet[16] __attribute__((aligned(4)));
static u8 ip[256] __attribute__((aligned(4)));
extern u32 rxfilter_legacy_set_config(const u8*);
extern u32 rxfilter_legacy_init_config(void);
static u8 command[16] __attribute__((aligned(4)));
static u8 raw_command[96] __attribute__((aligned(4)));
static u32 result[8];
static u32 families[2] = {1, 0};
static u32 multicast;
static u32 reason;
static u8 global[96] __attribute__((aligned(4)));
static u8 statistics[0x314] __attribute__((aligned(4)));
static void put(u8* p,u32 v) { *(u32*)p=v; }
static void net(u8* p,u32 v) {p[0]=v>>8;p[1]=v;}
static int compare(void) {
 u32 original=((u32(*)(const u8*))0xf036b81c)(descriptor);
 u32 replacement=rxfilter_legacy_bitmap(descriptor);
 ++result[0];
 if(original!=replacement){++result[1];result[2]=original;result[3]=replacement;return 1;}
 return 0;
}
int sim_memcmp(const u8* a,const u8* b,u32 n) {
 while(n--) {if(*a!=*b)return (int)*a-(int)*b; ++a;++b;}return 0;
}
u32 sim_count(u32 family) {return families[family];}
u32 sim_multicast(const u8* d) { (void)d; return multicast; }
void sim_reason(u32 value) { reason = value; }
static u32 random_value(void) {
 static u32 state = 0xa73fd9b1;
 state ^= state << 13; state ^= state >> 17; state ^= state << 5;
 return state;
}
int main(void) {
 put(descriptor+0x64,(u32)station);put(station+12,(u32)bss);
 put(descriptor+0x2c,(u32)ethernet);put(descriptor+0x30,(u32)ip);
 put((u8*)0xf03c65c8,(u32)list);
 u32* masks=(u32*)0xf03c6410;
 static const u32 ports[]={0,53,67,68,137,138,1900,5246,5353,5355,19132,32767,65535};
 net(ethernet+12,0x800);ip[9]=17;
 for(u32 mask=0;mask<8192;++mask){ masks[0]=mask;
  for(u32 a=0;a<13;++a)for(u32 b=0;b<13;++b)for(u32 local=0;local<2;++local){
   net(ip+20,ports[a]);net(ip+22,ports[b]);ip[16]=local?224:239;
   result[4]=mask;result[5]=ports[a];result[6]=ports[b];result[7]=local;
   if(compare())goto done;
  }
 }
 for(u32 type=0;type<65536;++type){
  net(ethernet+12,type);result[4]=type;
  for(u32 bits=0;bits<64;++bits){masks[4]=bits;result[5]=bits;if(compare())goto done;}
 }
 for(u32 round=0;round<65536;++round) {
  result[4]=round;
  u32 family=round%3;
  net(ethernet+12,family==0?0x800:family==1?0x86dd:random_value());
  for(u32 j=0;j<256;++j)ip[j]=random_value();
  for(u32 j=0;j<6;++j)masks[j]=random_value();
  for(u32 j=0;j<128;++j)list[j]=random_value();
  list[0x20]=round%5;list[0x65]=(round/5)%5;
  if(round&1) {
   for(u32 j=0;j<4;++j) list[j]=ip[16+j];
   for(u32 j=0;j<16;++j) list[0x25+j]=ip[24+j];
  }
  bss[0]=round%4;put((u8*)(0xf03c65c8+4*bss[0]),(u32)list);
  families[0]=round&2;families[1]=round&4;
  if(compare())goto done;
 }
 for(u32 round=0;round<4096;++round) {
  u32 index=round%4, bc=(round>>2)&255, mc=(round>>10)&1;
  u32 low=round&1?0xffffffff:0x341542, high=round&2?0xffffffff:0x63549;
  put(descriptor+0x64,round&4?(u32)station:0);
  put(bss+0x38,(u32)statistics);bss[0]=index;
  put((u8*)0xf03adc2c,(u32)global);
  *(u8*)0xf03add1e=(round>>11)&1;
  multicast=mc;families[0]=families[1]=0;
  put(statistics+0x2a8,0xffffffff);put(statistics+0x308,0x641735);put(statistics+0x310,0x4abcde);
  put(global+0x58,low);put(global+0x5c,high);reason=0;
  u32 original=((u32(*)(const u8*,u32))0xf032a398)(descriptor,bc);
  u32 old_low=*(u32*)(global+0x58),old_high=*(u32*)(global+0x5c),old_reason=reason;
  u32 old_stat=*(u32*)(statistics+0x2a8),old_stat2=*(u32*)(statistics+0x310);
  put(statistics+0x2a8,0xffffffff);put(statistics+0x310,0x4abcde);
  put(global+0x58,low);put(global+0x5c,high);reason=0;
  u32 replacement=rxfilter_legacy_wrapper(descriptor,bc);
  ++result[0];
  if(original!=replacement || old_low!=*(u32*)(global+0x58) ||
     old_high!=*(u32*)(global+0x5c) || old_reason!=reason ||
     old_stat!=*(u32*)(statistics+0x2a8) || old_stat2!=*(u32*)(statistics+0x310)) {
   ++result[1];result[2]=original;result[3]=replacement;result[4]=round;goto done;
  }
 }
 for(u32 byte=0;byte<256;++byte)for(u32 length=0;length<256;++length) {
  put(command+12,(u32)raw_command);
  raw_command[0x42]=byte;raw_command[0x43]=length;
  for(u32 j=0;j<6;++j)put(raw_command+0x44+4*j,random_value());
  u32 before[7],after[7];
  for(u32 j=0;j<7;++j)masks[j]=before[j]=random_value();
  u32 original=((u32(*)(const u8*))0xf036b6e0)(command);
  for(u32 j=0;j<7;++j){after[j]=masks[j];masks[j]=before[j];}
  u32 replacement=rxfilter_legacy_set_config(command);
  ++result[0];
  u32 difference=original!=replacement;
  for(u32 j=0;j<7;++j)difference|=after[j]!=masks[j];
  if(difference){++result[1];result[4]=byte;result[5]=length;goto done;}
 }
 for(u32 round=0;round<256;++round) {
  u32 before[7],after[7];
  for(u32 j=0;j<7;++j)masks[j]=before[j]=random_value();
  u32 original=((u32(*)(void))0xf036b754)();
  for(u32 j=0;j<7;++j){after[j]=masks[j];masks[j]=before[j];}
  u32 replacement=rxfilter_legacy_init_config();
  ++result[0];
  u32 difference=original!=replacement;
  for(u32 j=0;j<7;++j)difference|=after[j]!=masks[j];
  if(difference){++result[1];result[4]=round;goto done;}
 }
 done:;
 register u32 r0 __asm__("$r0")=1;
 register void* r1 __asm__("$r1")=result;
 register u32 r2 __asm__("$r2")=sizeof(result);
 __asm__ volatile("syscall 5" : "+r"(r0) : "r"(r1),"r"(r2) : "memory");
 return result[1]!=0;
}
