#ifndef H2GCHACTION_H
#define H2GCHACTION_H

#include "Action.h"
#include "DepInfo.h"

class H2GchAction : public Action {
public:
    bool execute(const DepInfo& info) override;
};

using H2GchActionPtr = shared_ptr<H2GchAction>;

inline
H2GchActionPtr makeH2GchAction()
{
    return H2GchActionPtr(new H2GchAction);
}


#endif // H2GCHACTION_H
