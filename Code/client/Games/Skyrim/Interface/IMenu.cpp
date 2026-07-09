
#include <Games/Skyrim/Interface/IMenu.h>

void IMenu::SetFlag(uint32_t auiFlag)
{
    SetMenuFlagsData(GetMenuFlagsData() | auiFlag);
}

void IMenu::ClearFlag(uint32_t auiFlag)
{
    SetMenuFlagsData(GetMenuFlagsData() & ~auiFlag);
}
