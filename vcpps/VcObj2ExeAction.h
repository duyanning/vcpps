#ifndef OBJ2EXEACTION_H
#define OBJ2EXEACTION_H

#include "Action.h"

class VcObj2ExeAction : public Action {
public:
    VcObj2ExeAction(string other_options);
    bool execute(const DepInfo& info) override;
private:
    string m_other_options;
};

using VcObj2ExeActionPtr = shared_ptr<VcObj2ExeAction>;

VcObj2ExeActionPtr makeVcObj2ExeAction(string other_options);

#endif // OBJ2EXEACTION_H
