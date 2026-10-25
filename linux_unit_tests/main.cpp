/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// RaftSysMods host-side tests
//
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <stdio.h>

int testBLEConnArbiter();

int main()
{
    int failCount = 0;
    failCount += testBLEConnArbiter();

    if (failCount > 0)
        printf("RaftSysMods linux_unit_tests FAILED %d tests\n", failCount);
    else
        printf("RaftSysMods linux_unit_tests all tests passed\n");
    return failCount > 0 ? 1 : 0;
}
