#include "Vkran.h"
#include "verilated.h"
#include <array>
#include <cstdio>
#include <cstdlib>
static Vkran d;
static std::array<unsigned char,65536> mem{};
static bool pending=false;
static unsigned pending_addr=0, reads=0,writes=0,ticks=0;
static void tick(){
 d.clk=0;d.mem_gnt=0;d.mem_ack=0;d.eval();
 if(pending && ticks%3==0){d.mem_ack=1;d.mem_q=mem[pending_addr];pending=false;}
 if(d.mem_req&&!pending&&ticks%2==0){
   d.mem_gnt=1;
   if(d.mem_we){mem[d.mem_addr&65535]=d.mem_wdat;writes++;}
   else{pending_addr=d.mem_addr&65535;pending=true;reads++;}
 }
 d.eval();d.clk=1;d.eval();ticks++;
}
static void reg(unsigned a,unsigned v){d.reg_addr=a;d.reg_din=v;d.reg_we=1;tick();d.reg_we=0;tick();}
static void run(unsigned mode,bool enabled,unsigned width=4,unsigned height=3,int stride=8){
 d.schub_enabled=enabled;reads=writes=0;
 const unsigned char job[]={0,1,0,0,2,0,(unsigned char)width,0,(unsigned char)height,0,1,0,(unsigned char)stride,(unsigned char)(stride>>8),0x44,(unsigned char)mode};
 for(unsigned i=0;i<16;i++)reg(i,job[i]);reg(0x10,1);
 for(unsigned n=0;d.busy&&n<10000;n++)tick();
 if(d.busy){std::fprintf(stderr,"KRAN hung\n");std::exit(1);}
}
static void require(bool c,const char* s){if(!c){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(int argc,char**argv){
 Verilated::commandArgs(argc,argv);d.reg_we=0;d.schub_enabled=0;d.reset=1;tick();d.reset=0;tick();
 for(unsigned i=0;i<16;i++)mem[0x100+i]=0x10+i;
 run(8,true);
 for(unsigned y=0;y<3;y++)for(unsigned x=0;x<4;x++)require(mem[0x200+y*8+x]==0x10+y,"replicated source column");
 require(reads==3&&writes==12,"one source read per row");
 run(8,false);
 for(unsigned y=0;y<3;y++)for(unsigned x=0;x<4;x++)require(mem[0x200+y*8+x]==0x10+y+x,"disabled SCHUB preserves old copy behavior");
 run(1,true);require(reads==0&&writes==12&&mem[0x200]==0x44,"legacy fill");
 run(8,true,1,3,-8);require(mem[0x200]==0x10&&mem[0x1f8]==0x11&&mem[0x1f0]==0x12,"single-pixel width and negative stride");
 run(8,true,0,3);require(reads==0&&writes==0,"zero width");
 std::puts("KRAN column repeat: PASS");
}
