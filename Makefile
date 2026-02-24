CC=mpicc
CFLAGS=-O3 -fopenmp
TARGET=life

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

# Default execute comamnds for different levels of parallelism.

run-serial:
	OMP_NUM_THREADS=1 mpiexec -np 1 ./life 4000 4000 200 42 0

run-mpi2:
	OMP_NUM_THREADS=1 mpiexec -np 2 ./life 4000 4000 200 42 0

run-mpi4:
	OMP_NUM_THREADS=1 mpiexec -np 4 ./life 4000 4000 200 42 0

run-omp2:
	OMP_NUM_THREADS=2 mpiexec -np 1 ./life 4000 4000 200 42 0

run-omp4:
	OMP_NUM_THREADS=4 mpiexec -np 1 ./life 4000 4000 200 42 0

run-hybrid:
	OMP_NUM_THREADS=4 mpiexec -np 2 ./life 4000 4000 200 42 0


# Default execute command for visualization.

run-vis:
	OMP_NUM_THREADS=1 mpiexec -np 1 ./life 20 20 200 42 1

# Can also run with the following command:
# mpiexec -np 4 ./life <rows> <cols> <generations> <seed> <visual>

clean:
	rm -f $(TARGET)

	