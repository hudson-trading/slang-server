module passthrough #(parameter type T = int)(input T value, output T result);
    assign result = value;
endmodule
