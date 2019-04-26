module hello #(parameter WIDTH = 4, parameter MAX = 15) (input clock, reset, output max);

logic [WIDTH-1:0] counter;
assign max = counter == MAX;
always @(posedge clock) begin
    if (reset || max) counter <= 0;
    else counter <= counter + 1;
end

endmodule
