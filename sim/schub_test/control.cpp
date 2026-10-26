#include "Vschub.h"
#include "verilated.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
static Vschub d;
static std::vector<unsigned char> memory(1<<24);
static unsigned requests=0, writes=0, clocks=0, updates=0;
static bool delayed=false,fast_delayed=false,block_ack=false;
static unsigned held_fast_address,held_fast_data,held_fast_we,held_fast_w16;
static unsigned held_address,held_data,held_we;
static void tick(){
 d.clk=0;d.mem_ack=0;d.fast_ack=0;d.eval();
 if(delayed && (!d.mem_req || d.mem_addr!=held_address || d.mem_din!=held_data || d.mem_we!=held_we)){std::fprintf(stderr,"Unstable memory request\n");std::exit(1);}
 if(fast_delayed && (!d.fast_req || d.fast_addr!=held_fast_address || d.fast_din!=held_fast_data || d.fast_we!=held_fast_we || d.fast_w16!=held_fast_w16)){std::fprintf(stderr,"Unstable fast request\n");std::exit(1);}
 // Delayed acknowledgements exercise stable-address and stop/drain behavior.
 if(d.mem_req && !block_ack && clocks%4==0){
   d.mem_ack=1;d.mem_q=memory[d.mem_addr];requests++;
   if(d.mem_we){memory[d.mem_addr]=d.mem_din;writes++;}
 }
 if(d.fast_req && !block_ack && clocks%5==0){
   d.fast_ack=1;unsigned a=d.fast_addr&~1u;d.fast_q=memory[a]|memory[a+1]<<8;requests++;
   if(d.fast_we){memory[d.fast_addr]=d.fast_din;if(d.fast_w16)memory[d.fast_addr+1]=d.fast_din>>8;writes++;}
 }
 fast_delayed=d.fast_req&&!d.fast_ack;held_fast_address=d.fast_addr;held_fast_data=d.fast_din;held_fast_we=d.fast_we;held_fast_w16=d.fast_w16;
 delayed=d.mem_req&&!d.mem_ack;held_address=d.mem_addr;held_data=d.mem_din;held_we=d.mem_we;
 d.eval();d.clk=1;d.eval();clocks++;if(d.osd_update)updates++;
}
static void cycles(unsigned n){while(n--)tick();}
static void reg(unsigned a,unsigned v){d.reg_addr=a;d.reg_din=v;d.reg_we=1;tick();d.reg_we=0;tick();}
static void require(bool condition,const char* text){if(!condition){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
static unsigned word(unsigned a){return memory[a]|memory[a+1]<<8|memory[a+2]<<16|memory[a+3]<<24;}
int main(int argc,char**argv){
 Verilated::commandArgs(argc,argv);d.reset=1;d.osd_enable=0;d.reg_we=0;d.reg_re=0;d.timer_irq=0;cycles(10);d.reset=0;cycles(20);
 d.reg_addr=0;d.eval();require(d.reg_dout=='S',"identification visible while disabled");
 require(requests==0&&!d.active,"disabled core has no bus traffic");
 const unsigned code[]={0x800010b7,0x01300113,0x01700193,0x02310233,0x0040a023,0x801002b7,0x0042a023,0x002280a3,0x00329123,0x0002a303,0x0060a223,0x001002b7,0x0002a303,0x0060a423,0x0000006f};
 for(unsigned i=0;i<sizeof(code)/4;i++)for(unsigned b=0;b<4;b++)memory[0x40000+4*i+b]=code[i]>>(b*8);
 d.osd_enable=1;cycles(20);require(!d.active&&requests==0,"OSD enable does not start an unconfigured program");
 reg(8,3);cycles(20000);require(word(0x1000)==437,"RV32IM multiplication and uncached store");
 require(word(0x100000)==0x001713b5,"fast word/byte/halfword write masks");
 require(word(0x1004)==0x001713b5&&word(0x1008)==0x001713b5,"cached and uncached fast reads agree");
 require(d.active,"software starts accelerator");
 d.osd_enable=0;cycles(200);require(!d.active&&!d.busy,"OSD off stops and drains bus");
 unsigned old=requests;cycles(200);require(requests==old,"disabled accelerator remains passive");
 for(int i=0;i<4;i++)memory[0x1000+i]=0;
 reg(8,3);cycles(20000);require(word(0x1000)==437,"software can enable after menu off; caches reset on restart");
 require(d.enabled_state&&updates==1,"software enable requests OSD feedback");
 d.osd_enable=1;cycles(20);require(d.active,"OSD feedback preserves running program");
 reg(8,0);cycles(200);require(!d.active&&!d.busy&&!d.enabled_state,"software disable");
 require(updates==2,"software disable updates OSD");
 d.osd_enable=0;cycles(20);
 // Change code before a new start to exercise instruction cache reset.
 const unsigned stopcode[]={0x8000d0b7,0xd0808093,0x00008023,0x0000006f};
 for(unsigned i=0;i<sizeof(stopcode)/4;i++)for(unsigned b=0;b<4;b++)memory[0x40000+4*i+b]=stopcode[i]>>(b*8);
 reg(8,3);cycles(1);
 // Stop while a delayed fetch is in flight; finish outstanding bytes safely.
 while(!d.mem_req&&!d.fast_req)tick();
 reg(8,0);cycles(200);require(!d.active&&!d.busy,"disable drains in-flight fetch");
 // Restart must not release the CPU while the previous transaction waits.
 for(unsigned i=0;i<sizeof(code)/4;i++)for(unsigned b=0;b<4;b++)memory[0x40000+4*i+b]=code[i]>>(b*8);
 reg(8,3);while(!d.fast_req)tick();block_ack=true;reg(8,0x83);cycles(100);
 require(!d.active&&d.busy,"restart waits for outstanding memory transaction");
 block_ack=false;cycles(20000);require(d.active&&word(0x1000)==437,"restart resumes cleanly");
 reg(8,0);cycles(200);
 // Out-of-range accesses report an error instead of wrapping into native RAM.
 const unsigned bad[]={0x010002b7,0x0002a303,0x0000006f};
 for(unsigned i=0;i<sizeof(bad)/4;i++)for(unsigned b=0;b<4;b++)memory[0x40000+4*i+b]=bad[i]>>(b*8);
 reg(8,3);cycles(10000);d.reg_addr=9;d.eval();require(d.reg_dout&8,"invalid physical address sets fault");
 reg(8,0);cycles(200);
 std::printf("SCHUB control: PASS; %u clocks, %u byte accesses, %u writes\n",clocks,requests,writes);
}
