#pragma once

// Hawthorne Link inside MELVILLE: MELVILLE owns the receiver (its NETWORK patch panel and
// window). Hawthorne uses the same one to tell senders where the tape is.

namespace hawthorne { class NetReceiver; }

namespace melville
{
    /** MELVILLE's Hawthorne Link receiver (null outside MELVILLE). */
    hawthorne::NetReceiver* networkReceiver() noexcept;
}
