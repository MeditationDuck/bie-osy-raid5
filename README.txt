g++ -Wall -Werror -Wextra -g expt.cpp
valgrind --leak-check=yes --undef-value-errors=no ./a.out 