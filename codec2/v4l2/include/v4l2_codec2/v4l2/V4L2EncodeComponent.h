// Copyright 2020 The Chromium Authors. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause

#ifndef ANDROID_V4L2_CODEC2_V4L2_V4L2_ENCODE_COMPONENT_H
#define ANDROID_V4L2_CODEC2_V4L2_V4L2_ENCODE_COMPONENT_H

#include <v4l2_codec2/components/EncodeComponent.h>

namespace android {

class V4L2EncodeComponent : public EncodeComponent {
public:

    static std::shared_ptr<C2Component> create(C2String name, c2_node_id_t id,
                                               std::shared_ptr<EncodeInterface> intfImpl,
                                               C2ComponentFactory::ComponentDeleter deleter);

    virtual ~V4L2EncodeComponent() override;

protected:
    bool initializeEncoder() override;

private:

    static std::atomic<int32_t> sConcurrentInstances;

    V4L2EncodeComponent(C2String name, c2_node_id_t id, std::shared_ptr<EncodeInterface> interface);
};

}

#endif
