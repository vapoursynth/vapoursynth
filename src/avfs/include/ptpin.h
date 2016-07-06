//---------------------------------------------------------------------------
// Copyright 1997-2014 Joe Lowe
//
// Permission is granted to any person obtaining a copy of this Software,
// to deal in the Software without restriction, including the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and sell copies of
// the Software.
//
// The above copyright and permission notice must be left intact in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS WITHOUT WARRANTY.
//---------------------------------------------------------------------------
// file name:  ptpin.h
// created:    2013.01.16
//
// Event broadcast object interface. Useful for event driven
// architectures, cancellation, and thread synchronization.
// ptsync.h provides a flexible implementation.
//
// The interfaces defined here are expected to be used as primitives
// in other interfaces, and so must indefinately maintain ABI
// compatibility. Source compatibility may change over time.
//---------------------------------------------------------------------------
#ifndef PTPIN_H
#define PTPIN_H
#include "ptpublic.h"

PT_TYPE_DEFINE(pin_wire_t)
{
   void (PT_CCALL*handler)(pin_wire_t*);
      // Implementation private data, treat as opaque.
      // Apart from the size, this definition does not
      // necessarily match any actual implementation.
   pin_wire_t** prev;
   pin_wire_t* next;
};
   // Client needs to zero-initialize before use only if
   // detach may be called for a wire where attach has
   // never been attempted. Term is only useful for
   // diagnostics.
PT_INLINE void pin_wire_init(pin_wire_t* w) { w->handler = 0; w->prev = 0; }
PT_INLINE void pin_wire_term(pin_wire_t* w) {
#ifdef ASSERT
      ASSERT(!w->prev);
#endif
   }

#define INTERFACE_NAME pin_i
PT_INTERFACE_DEFINE
{
   PT_INTERFACE_FUN0( void                 , signal );
   PT_INTERFACE_FUN0( void                 , reset  );
      // Attach a handler to be called when pin is signalled.
      // Attach will fail if pin already signalled. Detach will
      // fail if handler has already or is in process of being
      // called.
   PT_INTERFACE_FUNC( int/*bool success*/  , attach ,void (PT_CCALL*handler)(pin_wire_t*),pin_wire_t*);
   PT_INTERFACE_FUNC( int/*bool success*/  , detach ,pin_wire_t*);
   PT_INTERFACE_FUN0( int/*bool signalled*/, poll   );
#ifdef __cplusplus
   PT_INLINE void pulse() { reset(); }
#endif
};
#undef INTERFACE_NAME

PT_INLINE void pin_i_signal(pin_i* p) { PT_VCAL0(p,signal); }
PT_INLINE void pin_i_reset(pin_i* p) { PT_VCAL0(p,reset); }
PT_INLINE void pin_i_pulse(pin_i* p) { PT_VCAL0(p,reset); }
PT_INLINE int/*bool success*/ pin_i_attach(pin_i* p,void (PT_CCALL*handler)(pin_wire_t*),pin_wire_t* w) { return PT_VCALL(p,attach,handler,w); }
PT_INLINE int/*bool success*/ pin_i_detach(pin_i* p,pin_wire_t* w) { return PT_VCALL(p,detach,w); }
PT_INLINE int/*bool signalled*/ pin_i_poll(pin_i* p) { return PT_VCAL0(p,poll); }

#endif
