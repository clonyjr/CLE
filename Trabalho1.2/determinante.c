/**
 *  \file computeDet.c (implementation file)
 *
 *  \brief Computation of the determinant of a square matrix through the application of the Gaussian elimination method.
 *
 *  It reads the number of matrices whose determinant is to be computed and their order from a binary file. The
 *  coefficients of each matrix are stored line wise.
 *  The file name may be supplied by the user.
 *  Multithreaded implementation.
 *
 *  Generator thread of the intervening entities and definition of the intervening entities.
 *
 *  SYNOPSIS:
 *  <P><PRE>                computeDet [OPTIONS]
 *
 *                OPTIONS:
 *                 -f name --- set the file name (default: "coefData.bin")
 *                 -h      --- print this help.</PRE>
 *
 *  \author António Rui Borges - February 2019
 */

#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <libgen.h>
#include <math.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#include <mpi.h>
#include "determinante.h"

/** \brief number of square matrices whose determinant is to be computed */
static int nMat;

/** \brief order of the square matrices whose determinant is to be computed */
static unsigned int order;

/** \brief pointer to the storage area of matrices coefficients */
static double *mat;

/** \brief pointer to the storage area of matrices determinants */
static double *det;

/** \brief pointer to the binary stream associated with the file in processing */
static FILE *f;

/** \brief amount of matrix calculation per process */
int amountPerProcess;

/** \brief matrix to be filled */
double **matrix;

double *buffer;

/**
 *  \brief Main thread.
 *
 *  Its role is starting the simulation by generating the intervening entities (dispatcher and determinant computing
 *  threads) and waiting for their termination.
 *
 *  \param argc number arguments in the command line
 *  \param argv array of pointers to the arguments
 */
int main (int argc, char *argv[]){
	char *fName = "coefData.bin";                                                         /* file name, set to default */

	int totalProcesses, process_id;														/* number of processes and ids */

	/* process command line options */
	int opt;                                                                                        /* selected option */

	opterr = 0;
	do{
		switch ((opt = getopt (argc, argv, "f:h"))){
		case 'f': /* file name */
			if ((optarg[0] == '-') || ((optarg[0] == '\0'))){
				fprintf (stderr, "%s: file name is missing\n", basename (argv[0]));
				printUsage (basename (argv[0]));
				return EXIT_FAILURE;
			}
			fName = optarg;
			break;
		case 'h': /* help mode */
			printUsage (basename (argv[0]));
			return EXIT_SUCCESS;
		case '?': /* invalid option */
			fprintf (stderr, "%s: invalid option\n", basename (argv[0]));
			printUsage (basename (argv[0]));
			return EXIT_FAILURE;
		case -1:  break;
		}
	} while (opt != -1);
	if (optind < argc){
		fprintf (stderr, "%s: invalid format\n", basename (argv[0]));
		printUsage (basename (argv[0]));
		return EXIT_FAILURE;
	}

	/* Initializing text processing threads application defined thread id arrays */
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &totalProcesses);									/* MPI size com numero total de processos */
	MPI_Comm_rank(MPI_COMM_WORLD, &process_id);										/* MPI rank com id do processo */

	double StartTime, EndTime;                                                                                     /* time limits */
	StartTime = MPI_Wtime();

	if(process_id == MASTER) {
		printf ("Entrei processo master.\n");

		/* open the file for reading */
		openFile (fName);
		amountPerProcess = nMat / totalProcesses;
		buffer = (double *) malloc(sizeof(double) * order * order);

		for(int i = 0; i < totalProcesses; i++){

			fread(buffer, sizeof(double), nMat * order * order, f);

			if(i == MASTER){
				for(int x = 0; x < amountPerProcess; x++){
					matrix = (double *) malloc(sizeof(buffer) * order * order);
					detMatrix(matrix, 0, amountPerProcess, nMat);
				}
				MPI_Barrier(MPI_COMM_WORLD);

				// print work
				closeFileAndPrintDetValues();

				for(int j = 1; j < totalProcesses; j++){
					MPI_Recv(&matrix, amountPerProcess, MPI_DOUBLE, j, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
					MPI_Recv(&det, amountPerProcess, MPI_DOUBLE, j, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

					/* close file and print the values of the determinants */
					closeFileAndPrintDetValues ();
				}
				EndTime = MPI_Wtime();

				printf ("\nElapsed time = %.6f s\n", EndTime - StartTime);

			}
			else{
				MPI_Send(&amountPerProcess, 1, MPI_INT, i, 0, MPI_COMM_WORLD);
				MPI_Send(&matrix, amountPerProcess, MPI_DOUBLE, i, 0, MPI_COMM_WORLD);
				MPI_Send(&det, amountPerProcess, MPI_DOUBLE, i, 0, MPI_COMM_WORLD);
			}
		}
	} else if(process_id > MASTER) {
		printf ("Entrei processo worker %d.\n", process_id);
		MPI_Recv(&amountPerProcess, 1, MPI_INT, MASTER, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		matrix = (double *) malloc(sizeof(double) * nMat * order * order);
		MPI_Recv(&matrix, amountPerProcess, MPI_DOUBLE, MASTER, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		MPI_Recv(&det, amountPerProcess, MPI_DOUBLE, MASTER, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		for(int x = 0; x < amountPerProcess; x++)
			detMatrix(&matrix, 0, amountPerProcess, nMat);

		MPI_Barrier(MPI_COMM_WORLD);

		MPI_Send(&matrix, amountPerProcess, MPI_DOUBLE, MASTER, 0, MPI_COMM_WORLD);
		MPI_Send(&det, amountPerProcess, MPI_DOUBLE, MASTER, 0, MPI_COMM_WORLD);
	}

	MPI_Finalize();
	return 0;
}

/**
 *  \brief Open file and initialize internal data structure.
 *
 *  Operation carried out by the master.
 *
 *  \param fName file name
 */
void openFile (char fName[]){
	int i;                                                                                        /* counting variable */

	if (strlen (fName) > M)
		fprintf (stderr, "file name too long");

	if ((f = fopen (fName, "r")) == NULL)
		perror ("error on file opening for reading");

	if (fread (&nMat, sizeof (nMat), 1, f) != 1)
		fprintf (stderr, "%s\n", "error on reading header - number of stored matrices\n");

	if (fread (&order, sizeof (order), 1, f) != 1)
		fprintf (stderr, "%s\n", "error on reading header - order of stored matrices\n");

	if ((mat = malloc (N * sizeof (double) * order * order)) == NULL)
		fprintf (stderr, "%s\n", "error on allocating storage area for matrices coefficients\n");

	if ((det = malloc (nMat * sizeof (double))) == NULL)
		fprintf (stderr, "%s\n", "error on allocating storage area for determinant values\n");
}

/**
 *  \brief Close file and print the values of the determinants.
 *
 *  Operation carried out by the master
 */
void closeFileAndPrintDetValues (void){
	printf ("Closing and Printing Values...\n");

	int i, n;                                                                                     /* counting variable */

	if (fclose (f) == EOF)
		perror ("error on closing file");
	printf ("\n");

	for (n = 0; n < nMat; n++)
		printf ("The determinant of matrix %d is %.3e\n", n, det[n]);
	printf ("\n");
}

double detMatrix(double **a, int s, int end, int n) {
	printf ("Calculating...\n");
	int i, j, j1, j2;
	double det;
	double **m = NULL;

	det = 0;                      // initialize determinant of sub-matrix

	// for each column in sub-matrix
	for (j1 = s; j1 < end; j1++) {
		// get space for the pointer list
		m = (double **) malloc((n - 1) * sizeof(double *));

		printf ("Calculating 1...\n");
		for (i = 0; i < n - 1; i++)
			m[i] = (double *) malloc((n - 1) * sizeof(double));

		printf ("Calculating 2...\n");
		for (i = 1; i < n; i++)
			for (j = 0; j < n; j++)
				m[i - 1][j - 1] = a[i][j];

		printf ("Calculating 3...\n");
		int dim = n - 1;
		double fMatr[dim * dim];
		for (i = 0; i < dim; i++)
			for (j = 0; j < dim; j++)
				fMatr[i * dim + j] = m[i][j];

		det += pow(-1.0, 1.0 + j1 + 1.0) * a[0][j1] * detMatrixHelper(dim, fMatr);

		for (i = 0; i < n - 1; i++)
			free(m[i]);

		free(m);

	}
	printf ("\nDet %f\n", det);
	return (det);
}

double detMatrixHelper(int nDim, double *pfMatr) {
	printf ("Calculating Helper...\n");
	double fDet = 1.;
	double fMaxElem;
	double fAcc;
	int i, j, k, m;

	for (k = 0; k < (nDim - 1); k++){ 										// base row of matrix

		// search of line with max element
		fMaxElem = fabs(pfMatr[k * nDim + k]);
		m = k;
		for (i = k + 1; i < nDim; i++) {
			if (fMaxElem < fabs(pfMatr[i * nDim + k])) {
				fMaxElem = pfMatr[i * nDim + k];
				m = i;
			}
		}

		// permutation of base line (index k) and max element line(index m)
		if (m != k) {
			for (i = k; i < nDim; i++) {
				fAcc = pfMatr[k * nDim + i];
				pfMatr[k * nDim + i] = pfMatr[m * nDim + i];
				pfMatr[m * nDim + i] = fAcc;
			}
			fDet *= (-1.);
		}

		if (pfMatr[k * nDim + k] == 0.) return 0.0;

		// trianglulation of matrix
		for (j = (k + 1); j < nDim; j++){ 						// current row of matrix
			fAcc = -pfMatr[j * nDim + k] / pfMatr[k * nDim + k];
			for (i = k; i < nDim; i++)
				pfMatr[j * nDim + i] = pfMatr[j * nDim + i] + fAcc * pfMatr[k * nDim + i];
		}
	}

	for (i = 0; i < nDim; i++)
		fDet *= pfMatr[i * nDim + i]; // diagonal elements multiplication

	return fDet;
}

/**
 *  \brief Print command usage.
 *
 *  A message specifying how the program should be called is printed.
 *
 *  \param cmdName string with the name of the command
 */
static void printUsage (char *cmdName){
	fprintf (stderr, "\nSynopsis: %s [OPTIONS]\n"
			"  OPTIONS:\n"
			"  -f name --- set the file name (default: \"coefData.bin\")\n"
			"  -h      --- print this help\n", cmdName);
}
