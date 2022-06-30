kernel void gpuwrite(__global int* array, __global int *next, long iter)
{
    const size_t tid = get_global_id(0);
    printf("next[%u]=%u iter=%u\n", tid, next[tid], iter);
    for (long r = 0; r < iter; r++) {
        array[next[tid]+1] = 0xff;
	// if (r % 1000 == 0) printf("r=%d\n", r);
        next[tid] = array[next[tid]];
    }

}
