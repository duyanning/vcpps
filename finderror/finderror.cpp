#include <iostream>
#include <string>

using namespace std;

int main()
{
	int exit_code = 0;
	while (cin) {
		string line;
		getline(cin, line);
		// ): error
		if (cin) { // ���������һ�ж��һ��\n
			cout << line << endl;
		}
		else {
			cout << line;
		}
		if (line.find("): error ") != std::string::npos) {
			exit_code = 1;
		}
	}

	return exit_code;
}
