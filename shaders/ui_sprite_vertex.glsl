void main() {
    out_uv = in_uv;
    out_color = in_color;
    out_draw = push.draw;
    gl_Position = vec4(in_position * push.inverse_extent * 2.0 - 1.0, 0.0, 1.0);
}
