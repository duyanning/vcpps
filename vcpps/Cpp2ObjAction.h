#ifndef CPP2OBJACTION_H
#define CPP2OBJACTION_H

#include "Action.h"

class Cpp2ObjAction : public Action {
	std::string m_additional_options;
public:
	Cpp2ObjAction(std::string additional_options)
		: m_additional_options{ additional_options }
	{

	}
	bool execute(const DepInfo& info) override;
};

using Cpp2ObjActionPtr = shared_ptr<Cpp2ObjAction>;


inline
Cpp2ObjActionPtr makeCpp2ObjAction(std::string additional_options = "")
{
    return Cpp2ObjActionPtr(new Cpp2ObjAction(additional_options));
}

#endif // CPP2OBJACTION_H
