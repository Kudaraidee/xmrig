/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2018 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2018      Lee Clagett <https://github.com/vtnerd>
 * Copyright 2018-2019 SChernykh   <https://github.com/SChernykh>
 * Copyright 2018-2019 tevador     <tevador@gmail.com>
 * Copyright 2016-2019 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "crypto/common/VirtualMemory.h"
#include "backend/cpu/Cpu.h"
#include "base/io/log/Log.h"
#include "crypto/common/MemoryPool.h"
#include "crypto/common/portable/mm_malloc.h"


#ifdef XMRIG_FEATURE_HWLOC
#   include "crypto/common/NUMAMemoryPool.h"
#endif


#include <cinttypes>
#include <mutex>


namespace xmrig {

static IMemoryPool *pool = nullptr;
static std::mutex mutex;

constexpr size_t twoMiB = 2U * 1024U * 1024U;
constexpr size_t oneGiB = 1024U * 1024U * 1024U;

} // namespace xmrig


xmrig::VirtualMemory::VirtualMemory(size_t size, bool hugePages, bool oneGbPages, bool usePool, uint32_t node, size_t alignSize) :
    m_size(align(size)),
    m_node(node)
{
    if (usePool) {
        std::lock_guard<std::mutex> lock(mutex);
        if (hugePages && !pool->isHugePages(node) && allocateLargePagesMemory()) {
            return;
        }

        m_scratchpad = pool->get(m_size, node);
        if (m_scratchpad) {
            m_flags.set(FLAG_HUGEPAGES, pool->isHugePages(node));
            m_flags.set(FLAG_EXTERNAL,  true);

            return;
        }
    }

    if (oneGbPages && allocateOneGbPagesMemory()) {
        return;
    }

    if (hugePages && allocateLargePagesMemory()) {
        return;
    }

    m_scratchpad = static_cast<uint8_t*>(_mm_malloc(m_size, alignSize));
}


xmrig::VirtualMemory::~VirtualMemory()
{
    if (!m_scratchpad) {
        return;
    }

    if (m_flags.test(FLAG_EXTERNAL)) {
        std::lock_guard<std::mutex> lock(mutex);
        pool->release(m_node);
    }
    else if (isHugePages() || isOneGbPages()) {
        freeLargePagesMemory();
    }
    else {
        _mm_free(m_scratchpad);
    }
}


xmrig::HugePagesInfo xmrig::VirtualMemory::hugePages() const
{
    return { this };
}


#ifndef XMRIG_FEATURE_HWLOC
uint32_t xmrig::VirtualMemory::bindToNUMANode(int64_t)
{
    return 0;
}
#endif


void xmrig::VirtualMemory::destroy()
{
    delete pool;
}


void xmrig::VirtualMemory::init(size_t poolSize, bool hugePages)
{
    if (!pool) {
        osInit(hugePages);
    }

#   ifdef XMRIG_FEATURE_HWLOC
    if (Cpu::info()->nodes() > 1) {
        pool = new NUMAMemoryPool(align(poolSize, Cpu::info()->nodes()), hugePages);
    } else
#   endif
    {
        pool = new MemoryPool(poolSize, hugePages);
    }
}
