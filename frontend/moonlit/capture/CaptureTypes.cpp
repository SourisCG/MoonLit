#include "CaptureTypes.hpp"

namespace MoonLit {

bool CaptureTarget::isValid() const
{
	if (processId == 0 && name.empty()) {
		return false;
	}
	return std::visit([](const auto &handle) {
		using T = std::decay_t<decltype(handle)>;
		if constexpr (std::is_same_v<T, uintptr_t>) {
			return handle != 0;
		} else if constexpr (std::is_same_v<T, void *>) {
			return handle != nullptr;
		} else {
			return !handle.empty();
		}
	}, window);
}

} // namespace MoonLit
