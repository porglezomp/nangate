`define POS0 3'b000
`define NEG0 3'b100
`define POS1 3'b001
`define NEG1 3'b101
`define PINF 3'b010
`define NINF 3'b110
`define PNAN 3'b011
`define NNAN 3'b111
`define ANAN 3'b?11
`define ANYF 3'b???

// A NaN gate! Computes `Inf - max(x+y, -Inf)`.
// See http://tom7.org/nand/
module nan(
    input      [2:0] A,
    input      [2:0] B, output reg [2:0] Y
);

always begin
    casez ({A, B})
    {`ANYF, `ANAN}, {`ANAN, `ANYF}: Y = `PINF;
    {`PINF, `NINF}, {`NINF, `PINF}: Y = `PINF;
    {`ANYF, `PINF}, {`PINF, `ANYF}: Y = `PNAN;
    default: Y = `PINF;
    endcase
end
endmodule

// Convert a bit into an fp3
module bit_to_fp3(input A, output [2:0] Y);
assign Y = A ? `PNAN : `PINF;
endmodule

// Convert an fp3 into a bit
module fp3_to_bit(input [2:0] A, output reg Y);
always begin
    case (A)
    `PNAN, `NNAN: Y = 1'b1;
    default: Y = 1'b0;
    endcase
end
endmodule

