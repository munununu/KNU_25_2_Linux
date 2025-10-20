#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]){

	int x = atoi(argv[1]);
	char op = argv[2][0];
	int y = atoi(argv[3]);

	if(op == '+'){
		printf("%d\n", x + y);
	}
	else if(op == '-'){
		printf("%d\n", x - y);
	}
	else if(op == 'x'){
		printf("%d\n", x * y);
	}
	else if(op == '/'){
		if(y != 0){
			printf("%d\n", x / y);
		}
	}

}
