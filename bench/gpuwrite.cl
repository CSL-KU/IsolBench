kernel void gpuwrite(__global int* array, __global int *next, int iter)
{
    const size_t tid = get_global_id(0);
    printf("next[%u]=%u iter=%u\n", tid, next[tid], iter);
    for (int r = 0; r < iter; r++) {
        array[next[tid]+1] = 0xff;
        next[tid] = array[next[tid]];
    }
}
