#pragma once

#include <sema/Context.h>

namespace sema {

//! A simple error handler
/*!
The class should be a reference for more complex handlers.
It should demonstarate how to print an error message with a source location
and how to recover in different situations.
*/
struct SimpleErrorHandler : ErrorHandler {

void handleError(Generator&, ErrorBase&) override;

};

}