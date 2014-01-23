//Author: Jonathon Jongsma
/*
 *This file is part of the Nemiver project
 *
 *Nemiver is free software; you can redistribute
 *it and/or modify it under the terms of
 *the GNU General Public License as published by the
 *Free Software Foundation; either version 2,
 *or (at your option) any later version.
 *
 *Nemiver is distributed in the hope that it will
 *be useful, but WITHOUT ANY WARRANTY;
 *without even the implied warranty of
 *MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *See the GNU General Public License for more details.
 *
 *You should have received a copy of the
 *GNU General Public License along with Nemiver;
 *see the file COPYING.
 *If not, see <http://www.gnu.org/licenses/>.
 *
 *See COPYRIGHT file copyright information.
 */
#ifndef __NMV_SAVED_SESSIONS_DIALOG_H__
#define __NMV_SAVED_SESSIONS_DIALOG_H__

#include <gtkmm/treeview.h>
#include <gtkmm/liststore.h>
#include "common/nmv-safe-ptr-utils.h"
#include "nmv-sess-mgr.h"
#include "nmv-dialog.h"

NEMIVER_BEGIN_NAMESPACE (nemiver)

namespace common {
class UString;
}

using nemiver::common::UString;
using nemiver::common::SafePtr;

class SavedSessionsDialog : public Dialog {
    class Priv;
    friend class Priv;
    SafePtr<SavedSessionsDialog::Priv> m_priv;

public:
    SavedSessionsDialog (const UString &a_root_path,
                         ISessMgr *a_sesssion_manager);
    virtual ~SavedSessionsDialog ();

    ISessMgr::Session session () const;
    void session (const ISessMgr::Session &a_session) const;

};//end class nemiver

NEMIVER_END_NAMESPACE (nemiver)

#endif //__NMV_SAVED_SESSIONS_DIALOG_H__

