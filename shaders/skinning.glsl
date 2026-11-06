vec3 Skinned(uint base, float weight_sum) {
    mat4 s = skin[base + in_joints.x] * in_weights.x + skin[base + in_joints.y] * in_weights.y + skin[base + in_joints.z] * in_weights.z +
             skin[base + in_joints.w] * in_weights.w;
    s /= weight_sum;
    return (s * vec4(in_position, 1.0)).xyz;
}
