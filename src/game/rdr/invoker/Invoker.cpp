#include "Invoker.hpp"

#include "Crossmap.hpp"
#include "game/pointers/Pointers.hpp"

namespace YimMenu
{
	void NativeInvoker::DefaultHandler(rage::scrNativeCallContext* ctx)
	{
		LOG(FATAL) << "Native handler not registered";
		ctx->SetReturnValue(0);
	}

	void NativeInvoker::CacheHandlers()
	{
		if (m_AreHandlersCached)
			return;

		for (int i = 0; i < g_Crossmap.size(); i++)
		{
			m_Handlers[i] = Pointers.GetNativeHandler(g_Crossmap[i]);
		}

		m_AreHandlersCached = true;
	}
}