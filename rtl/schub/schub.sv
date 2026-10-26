// SCHUB: optional, programmable RV32IM companion for MERIDIAN.
// GPL-3.0-or-later. VexRiscv itself is MIT licensed (separate notice).
// Register page $00:CD00. Software and OSD changes use last-writer-wins;
// switching off drains the current memory transfer before resetting the CPU.
`timescale 1ns/1ps
module schub (
    input wire clk, reset,
    input wire osd_enable,
    input wire [7:0] reg_addr, reg_din,
    input wire reg_we, reg_re,
    output reg [7:0] reg_dout,
    input wire timer_irq,
    // Shared MERIDIAN byte bus. A request stays stable through any wait.
    output reg mem_req,
    output reg [23:0] mem_addr,
    output reg mem_we,
    output reg [7:0] mem_din,
    input wire mem_ack,
    input wire [7:0] mem_q,
    // SDRAM accesses use complete halfwords without occupying the 65C816 bus.
    output wire fast_req, fast_we, fast_w16,
    output wire [23:0] fast_addr,
    output wire [15:0] fast_din,
    input wire fast_ack,
    input wire [15:0] fast_q,
    output wire enabled_state,
    output reg osd_update,
    output wire active,
    output wire busy
);
    reg enabled, requested_run, osd_previous;
    reg [31:0] entry, argument, result;
    reg [31:0] cycles, cycle_snapshot;
    reg fault;
    reg [7:0] reset_delay;
    wire running = enabled && requested_run;
    wire cpu_reset = reset || (!running && !busy) || (reset_delay != 0);
    assign enabled_state = enabled;
    assign active = running && reset_delay == 0;
    assign busy = state != IDLE;

    wire i_cyc, i_stb, d_cyc, d_stb, d_we;
    wire [29:0] i_adr, d_adr;
    wire [31:0] d_data;
    wire [3:0] d_sel;
    reg i_ack, d_ack, i_err, d_err;
    reg [31:0] bus_data;
    VexRiscv cpu (
        .clk(clk), .reset(cpu_reset), .externalResetVector(entry),
        .timerInterrupt(timer_irq), .softwareInterrupt(1'b0), .externalInterruptArray(32'd0),
        .iBusWishbone_CYC(i_cyc), .iBusWishbone_STB(i_stb), .iBusWishbone_ADR(i_adr),
        .iBusWishbone_ACK(i_ack), .iBusWishbone_ERR(i_err), .iBusWishbone_DAT_MISO(bus_data),
        .iBusWishbone_WE(), .iBusWishbone_DAT_MOSI(), .iBusWishbone_SEL(), .iBusWishbone_CTI(), .iBusWishbone_BTE(),
        .dBusWishbone_CYC(d_cyc), .dBusWishbone_STB(d_stb), .dBusWishbone_ADR(d_adr),
        .dBusWishbone_WE(d_we), .dBusWishbone_DAT_MOSI(d_data), .dBusWishbone_SEL(d_sel),
        .dBusWishbone_ACK(d_ack), .dBusWishbone_ERR(d_err), .dBusWishbone_DAT_MISO(bus_data),
        .dBusWishbone_CTI(), .dBusWishbone_BTE()
    );

    localparam IDLE=0, BYTE=1, RESPONSE=2, RELEASE=3;
    reg [1:0] state;
    reg owner_data;
    reg [31:0] address, write_data;
    reg [3:0] lanes;
    reg write_enable;
    reg [2:0] lane;
    wire selected_lane = lanes[lane[1:0]];
    // Only the 16-MB MERIDIAN space and its uncached alias are implemented.
    wire invalid_address = address[30:24] != 0;
    wire fast_address = address[23:16]>=8'h04 && address[23:16]!=8'hfd && address[23:16]!=8'hff;
    wire pair = !lane[0] && lanes[lane[1:0]+1'b1];
    assign fast_req = state==BYTE && lane<4 && selected_lane && !invalid_address && fast_address;
    assign fast_we = write_enable;
    assign fast_w16 = pair;
    assign fast_addr = address[23:0]+{22'd0,lane[1:0]};
    assign fast_din = write_data >> (8*lane[1:0]);

    always @* begin
        mem_req = state == BYTE && lane < 4 && selected_lane && !invalid_address && !fast_address;
        mem_addr = address[23:0] + {22'd0,lane[1:0]};
        mem_we = write_enable;
        mem_din = write_data[8*lane[1:0]+:8];
        case(reg_addr)
          8'h00:reg_dout="S"; 8'h01:reg_dout="C"; 8'h02:reg_dout="H"; 8'h03:reg_dout="B";
          8'h04:reg_dout=1; // interface version
          8'h05:reg_dout=31; // RV32IM, caches, alias, SDRAM halfwords, KRAN column repeat
          8'h08:reg_dout={6'd0,requested_run,enabled};
          8'h09:reg_dout={4'b1000,fault,busy,active,enabled};
          8'h0c,8'h0d,8'h0e,8'h0f:reg_dout=entry[8*reg_addr[1:0]+:8];
          8'h10,8'h11,8'h12,8'h13:reg_dout=argument[8*reg_addr[1:0]+:8];
          8'h14,8'h15,8'h16,8'h17:reg_dout=result[8*reg_addr[1:0]+:8];
          8'h18:reg_dout=cycles[7:0];
          8'h19,8'h1a,8'h1b:reg_dout=cycle_snapshot[8*reg_addr[1:0]+:8];
          8'h1c:reg_dout=8'h00; 8'h1d:reg_dout=8'h36;
          8'h1e:reg_dout=8'h6e; 8'h1f:reg_dout=8'h01; // 24,000,000 Hz
          default:reg_dout=0;
        endcase
    end

    always @(posedge clk) begin
        i_ack<=0;d_ack<=0;i_err<=0;d_err<=0;osd_update<=0;
        osd_previous<=osd_enable;
        if(active)cycles<=cycles+1'b1;
        if(reset_delay!=0 && !busy)reset_delay<=reset_delay-1'b1;
        if(reg_re && reg_addr==8'h18)cycle_snapshot<=cycles;
        if(osd_enable!=osd_previous)begin
            enabled<=osd_enable;
            if(!osd_enable)requested_run<=0;
        end
        if(reg_we)begin
            case(reg_addr)
              8'h08:begin
                  enabled<=reg_din[0];requested_run<=reg_din[0]&&reg_din[1];
                  if(enabled!=reg_din[0])osd_update<=1;
                  if(!requested_run || reg_din[7])begin reset_delay<=8;cycles<=0;fault<=0;end
              end
              8'h0c,8'h0d,8'h0e,8'h0f:if(!requested_run)entry[8*reg_addr[1:0]+:8]<=reg_din;
              8'h10,8'h11,8'h12,8'h13:argument[8*reg_addr[1:0]+:8]<=reg_din;
              8'h14,8'h15,8'h16,8'h17:result[8*reg_addr[1:0]+:8]<=reg_din;
              default:;
            endcase
        end
        case(state)
          IDLE:if(active && ((d_cyc&&d_stb)||(i_cyc&&i_stb)))begin
              owner_data<=d_cyc&&d_stb;
              address<={(d_cyc&&d_stb)?d_adr:i_adr,2'b00};
              write_data<=d_data;write_enable<=d_cyc&&d_stb&&d_we;
              lanes<=(d_cyc&&d_stb)?d_sel:4'b1111;
              lane<=0;bus_data<=0;state<=BYTE;
          end
          BYTE:begin
              if(invalid_address)begin fault<=1;state<=RESPONSE;end
              else if(lane==4)state<=RESPONSE;
              else if(!selected_lane)lane<=lane+1'b1;
              else if(fast_address && fast_ack)begin
                  if(!write_enable)begin
                      bus_data[8*lane[1:0]+:8]<=lane[0]?fast_q[15:8]:fast_q[7:0];
                      if(pair)bus_data[8*(lane[1:0]+1'b1)+:8]<=fast_q[15:8];
                  end
                  lane<=lane+(pair?3'd2:3'd1);
              end
              else if(!fast_address && mem_ack)begin
                  if(!write_enable)bus_data[8*lane[1:0]+:8]<=mem_q;
                  lane<=lane+1'b1;
              end
          end
          RESPONSE:begin
              if(owner_data)begin d_ack<=!invalid_address;d_err<=invalid_address;end
              else begin i_ack<=!invalid_address;i_err<=invalid_address;end
              state<=RELEASE;
          end
          RELEASE:state<=IDLE; // master consumes the registered ACK at this edge
        endcase
        if(reset)begin
            enabled<=osd_enable;osd_previous<=osd_enable;requested_run<=0;osd_update<=0;
            entry<=32'h00040000;argument<=0;result<=0;cycles<=0;cycle_snapshot<=0;fault<=0;
            reset_delay<=8;state<=IDLE;owner_data<=0;address<=0;
            write_data<=0;write_enable<=0;lanes<=0;lane<=0;bus_data<=0;
            i_ack<=0;d_ack<=0;i_err<=0;d_err<=0;
        end
    end
endmodule
