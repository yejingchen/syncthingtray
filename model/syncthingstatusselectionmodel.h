#ifndef DATA_SYNCTHINGSTATUSSELECTIONMODELL_H
#define DATA_SYNCTHINGSTATUSSELECTIONMODELL_H

#include "./global.h"

#include <qtutilities/models/checklistmodel.h>

namespace Data {

class LIB_SYNCTHING_MODEL_EXPORT SyncthingStatusSelectionModel : public Models::ChecklistModel {
    Q_OBJECT

public:
    explicit SyncthingStatusSelectionModel(QObject *parent = nullptr);
    QString labelForId(const QVariant &id) const override;

protected:
};

} // namespace Data

#endif // DATA_SYNCTHINGSTATUSSELECTIONMODELL_H
