module audio_sample_tick (
    input wire clk,             // 24.576 MHz clock
    input wire rst,             // Reset
    output reg sample_tick      // Pulso de un ciclo a 44.1 kHz
);

    // Calculamos el incremento de fase para 44.1 kHz con 24.576 MHz
    // f_out = f_clk * (phase_inc / 2^N)
    // phase_inc = 2^N * f_out / f_clk

    // Usamos un acumulador de 32 bits
    localparam PHASE_INC = 32'd773094113; // ≈ 2^32 * 44_100 / 24_576_000

    reg [31:0] phase_acc = 0;

    always @(posedge clk or posedge rst) begin
        if (rst) begin
            phase_acc <= 0;
            sample_tick <= 0;
        end else begin
            phase_acc <= phase_acc + PHASE_INC;
            sample_tick <= (phase_acc < PHASE_INC); // genera un pulso cuando hay overflow
        end
    end
endmodule
