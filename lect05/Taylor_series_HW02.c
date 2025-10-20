#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#define _USE_MATH_DEFINES
#include <math.h>
#define N 4

void sinx_taylor(int num_elements, int terms, double* x, double* result){

	int fd[N][2];
	pid_t pids[N];

	for(int i=0; i<num_elements; i++){
	
		if(pipe(fd[i]) == -1){
			perror("pipe error");
			return ;		
		}

		pids[i] = fork();

		if(pids[i] < 0){
			perror("fork error");
			return;
		}

		if(pids[i] == 0){
			close(fd[i][0]); // child read close

			double value = x[i];
			double numer = x[i] * x[i] * x[i];
			double denom = 6.; // 3!
			int sign = -1;

			for(int j=1; j<=terms; j++){
				value += (double)sign * numer / denom;
				numer *= x[i] * x[i];
				denom *= (2.*(double)j+2.) * (2.*(double)j+3.);
				sign *= -1;
			}
		
			write(fd[i][1], &value, sizeof(double));

			close(fd[i][1]); // child write close
			exit(0);
		}
	
		close(fd[i][1]); // parent write close
	}

	for(int i=0; i<num_elements; i++){
		waitpid(pids[i], NULL, 0);

		read(fd[i][0], &result[i], sizeof(double));

		close(fd[i][0]); // parent read close
	}

}


int main() {
	double x[N] = {0, M_PI/6., M_PI/3., 0.134};
	double res[N];

	sinx_taylor(N, 3, x, res);
	for(int i=0; i<N; i++){
		printf("sin(%.2f) by Taylor series = %f\n", x[i], res[i]);
		printf("sin(%.2f) = %f\n", x[i], sin(x[i]));
	}

	return 0;
}
