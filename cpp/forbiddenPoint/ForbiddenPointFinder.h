#if !defined(FORBIDDENPOINTFINDER_H_INCLUDED_)
#define FORBIDDENPOINTFINDER_H_INCLUDED_

#include <stdint.h>
#include "../game/board.h"


//#define BLACKFIVE 24
//#define WHITEFIVE 25
//#define BLACKFORBIDDEN 26

class CForbiddenPointFinder
{
public:

	int f_boardsize; // default = 15;
private:

public:
	char cBoard[COMPILE_MAX_BOARD_LEN + 2][COMPILE_MAX_BOARD_LEN + 2];
	CForbiddenPointFinder();
	CForbiddenPointFinder(int size);
	virtual ~CForbiddenPointFinder();

	void Clear();
	//int  AddStone(int x, int y, char cStone);
	bool isForbidden(int x, int y);
	bool isForbiddenNoNearbyCheck(int x, int y);
	void SetStone(int x, int y, char cStone);
	bool IsFive(int x, int y, int nColor);
	bool IsOverline(int x, int y);
	bool IsFive(int x, int y, int nColor, int nDir);
	bool IsFour(int x, int y, int nColor, int nDir);
	int  IsOpenFour(int x, int y, int nColor, int nDir);
	bool IsOpenThree(int x, int y, int nColor, int nDir	);
	bool IsDoubleFour(int x, int y);
	bool IsDoubleThree(int x, int y);

};
#endif // !defined(FORBIDDENPOINTFINDER_H_INCLUDED_)