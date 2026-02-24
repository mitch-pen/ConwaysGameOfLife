#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <mpi.h>
#include <omp.h>
#include <unistd.h>  // for sleep() in the visualisation.

typedef enum { DEAD=0, ALIVE=1 } State;


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

    for (int r = (row-1); r <= (row+1); r++) {
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
 * rows: Number of total rows.
 * cols: Number of total columns.
 * seed: given Seed.
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
 * 
 * argc: Number of arguments.
 * argv: Value of arguments.
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

    /* Allocate grids dynamically */
    State *grid = malloc(rows*cols*sizeof(State));
    State *next_grid = malloc(rows*cols*sizeof(State));

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
    int start = rank * rows_per_proc;
    int end = (rank == size-1) ? rows : start + rows_per_proc;

    /* Sychronize MPI processes */
    MPI_Barrier(MPI_COMM_WORLD);
    double t_start = MPI_Wtime();

    for (int g = 0; g < generations; g++) {

        /* Begin Parallel Execution of the for loop */
        #pragma omp parallel for schedule(static)
        for (int i = start; i < end; i++) {
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

        /* Gather all rows from all processes */
        MPI_Allgather(&next_grid[start*cols],
                      (end-start)*cols,
                      MPI_INT,
                      grid,
                      (end-start)*cols,
                      MPI_INT,
                      MPI_COMM_WORLD);

        /* Visualisation on rank 0 */
        if (visual && rank == 0) {
            display(grid, rows, cols);
            sleep(1);  // Sleep 1 second between generations
        }
    }

    /* Resynchronize MPI processes before ending the timer */
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
        printf("OpenMP Threads     : %d\n", omp_get_max_threads());
        printf("Total Hardware     : %d logical workers\n", size * omp_get_max_threads());
        printf("Runtime            : %f seconds\n", runtime);
        printf("Time Complexity    : O(G * R * C)\n");
        printf("Space Complexity   : O(R * C)\n");
        printf("Memory Used        : %.2f MB\n", memory_bytes/(1024.0*1024.0));
        printf("===============================\n");
    }

    /* Free allocated memory. */
    free(grid);
    free(next_grid);

    /* End MPI */
    MPI_Finalize();

    return 0;
}