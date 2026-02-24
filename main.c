#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mpi.h>
#include <omp.h>
#include <unistd.h>  // for sleep() in the visualisation.

typedef enum { DEAD=0, ALIVE=1 } State;

/* Visible to all functions */
State *grid;
State *next_grid;
int local_start, local_end;

/**
 * Count the number of alive neighbours
 * row: Row of current cell
 * col: Column of current cell
 * grid: Grid cells
 * rows: Total number of rows.
 * cols: Total number of columns.
 */
int neighbour_count(int row, int col, State *grid, int rows, int cols) {
    int count=0;
    for (int r = row-1; r <= row+1; r++) {
        for (int c = col-1; c <= col+1; c++) {
            if (r >= 0 && r < rows &&
                c >= 0 && c < cols &&
                !(r == row && c == col)) {
                count += grid[r*cols + c];
            }
        }
    }
    return count;
}

/**
 * Initialize grid with set seed.
 * 
 * grid: Grid to initialize.
 * rows: Total number of rows.
 * cols: Total number of cols.
 * seed: Set seed.
 */
void init(State *grid, int rows, int cols, int seed) {
    srand(seed);
    for (int i = 0; i < (rows*cols); i++)
        grid[i] = rand() % 2;
}

/**
 * Print the grid to the terminal.
 * 
 * grid: Grid to display.
 * rows: Total number of rows.
 * cols: Total number of cols.
 */
void display(State *grid, int rows, int cols) {
    printf("\033[2J\033[H"); // Clear terminal (only Linux, moves terminal down)
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++)
            printf(grid[i*cols + j] ? "X" : " ");
        printf("\n");
    }
}

/**
 * Main program.
 */
int main(int argc, char** argv) {

    MPI_Init(&argc, &argv);

    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if (argc != 6) {
        if (rank == 0)
            printf("Usage: %s <rows> <cols> <generations> <seed> <visual 0/1>\n", argv[0]);
        MPI_Finalize();
        return 1;
    }

    /* Get Command Line Arguments */
    int rows = atoi(argv[1]);
    int cols = atoi(argv[2]);
    int generations = atoi(argv[3]);
    int seed = atoi(argv[4]);
    int visual = atoi(argv[5]);

    /* Set OpenMP threads automatically to max available */
    int thread_count = omp_get_max_threads();
    omp_set_num_threads(thread_count);

    /* Allocate grids dynamically */
    grid = malloc(rows*cols*sizeof(State));
    next_grid = malloc(rows*cols*sizeof(State));

    if (!grid || !next_grid) {
        if (rank == 0) printf("Error: Unable to allocate memory for grids.\n");
        MPI_Finalize();
        return 1;
    }

    /* Initialise grid on rank 0 */
    if (rank == 0)
        init(grid, rows, cols, seed);

    /* Broadcast initial grid to all processes */
    MPI_Bcast(grid, rows*cols, MPI_INT, 0, MPI_COMM_WORLD);

    /* Divide GameOfLife rows evenly among MPI processes */
    int rows_per_proc = rows / size;
    local_start = rank * rows_per_proc;
    local_end = (rank == size-1) ? rows : local_start + rows_per_proc;

    int top = rank - 1;
    int bottom = rank + 1;
    if (top < 0) top = MPI_PROC_NULL;  // no top neighbour
    if (bottom >= size) bottom = MPI_PROC_NULL; // no bottom neighbour

    /* Synchronize parallel workers */
    MPI_Barrier(MPI_COMM_WORLD);
    double t_start = MPI_Wtime();

    /* Temporary buffers for halo rows */
    State *top_row = malloc(cols * sizeof(State));
    State *bottom_row = malloc(cols * sizeof(State));

    for (int g = 0; g < generations; g++) {

        /* Exchange first/last row with neighbours asynchronously */
        MPI_Request reqs[4];
        MPI_Isend(&grid[local_start*cols], cols, MPI_INT, top, 0, MPI_COMM_WORLD, &reqs[0]);
        MPI_Irecv(top_row, cols, MPI_INT, top, 1, MPI_COMM_WORLD, &reqs[1]);
        MPI_Isend(&grid[(local_end-1)*cols], cols, MPI_INT, bottom, 1, MPI_COMM_WORLD, &reqs[2]);
        MPI_Irecv(bottom_row, cols, MPI_INT, bottom, 0, MPI_COMM_WORLD, &reqs[3]);

        /* Compute inner rows (excluding first/last row of this chunk) */
        #pragma omp parallel for collapse(2)
        for (int i = local_start + 1; i < local_end - 1; i++) {
            for (int j = 0; j < cols; j++) {
                int n = neighbour_count(i, j, grid, rows, cols);
                if (n == 2)
                    next_grid[i*cols + j] = grid[i*cols + j];
                else if (n == 3)
                    next_grid[i*cols + j] = ALIVE;
                else
                    next_grid[i*cols + j] = DEAD;
            }
        }

        /* Wait for halo rows to arrive */
        MPI_Waitall(4, reqs, MPI_STATUSES_IGNORE);

        /* Compute boundary rows (top and bottom of this process) */
        int i = local_start;
        if (top != MPI_PROC_NULL) {
            for (int j = 0; j < cols; j++) {
                int n = neighbour_count(i, j, grid, rows, cols);
                if (n == 2)
                    next_grid[i*cols + j] = grid[i*cols + j];
                else if (n == 3)
                    next_grid[i*cols + j] = ALIVE;
                else
                    next_grid[i*cols + j] = DEAD;
            }
        }

        i = local_end-1;
        if (bottom != MPI_PROC_NULL) {
            for (int j = 0; j < cols; j++) {
                int n = neighbour_count(i, j, grid, rows, cols);
                if (n == 2)
                    next_grid[i*cols + j] = grid[i*cols + j];
                else if (n == 3)
                    next_grid[i*cols + j] = ALIVE;
                else
                    next_grid[i*cols + j] = DEAD;
            }
        }

        /* Swap grids */
        State *tmp = grid;
        grid = next_grid;
        next_grid = tmp;

        /* Visualisation on rank 0 if flag is set */
        if (visual && rank == 0) {
            display(grid, rows, cols);
            usleep(100000); // 0.1s between generations
        }
    }

    /* Resynchronize parallel workers before ending */
    MPI_Barrier(MPI_COMM_WORLD);
    double t_end = MPI_Wtime();

    /* Print runtime statistics */
    if (rank == 0) {
        double runtime = t_end - t_start;
        long long memory_bytes = 2LL * rows * cols * sizeof(State);

        printf("\n===== PERFORMANCE REPORT =====\n");
        printf("Grid Size          : %d x %d\n", rows, cols);
        printf("Generations        : %d\n", generations);
        printf("Seed               : %d\n", seed);
        printf("MPI Processes      : %d\n", size);
        printf("OpenMP Threads     : %d\n", thread_count);
        printf("Total Hardware     : %d logical workers\n", size * thread_count);
        printf("Runtime            : %f seconds\n", runtime);
        printf("Time Complexity    : O(G * R * C)\n");
        printf("Space Complexity   : O(R * C)\n");
        printf("Memory Used        : %.2f MB\n", memory_bytes/(1024.0*1024.0));
        printf("===============================\n");
    }

    /* Free Memory */
    free(grid);
    free(next_grid);
    free(top_row);
    free(bottom_row);

    MPI_Finalize();
    return 0;
}
