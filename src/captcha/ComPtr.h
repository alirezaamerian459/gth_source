#pragma once

// Minimal RAII wrapper so COM calls can hand back a T** the normal way (&ptr) - shared by every
// native UI Automation consumer in this module (SonyLinkCopyWatcher, CaptchaContextService,
// PatchAutoClicker), which each used to keep their own private copy of this exact template.
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ~ComPtr() { if (p_) p_->Release(); }
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    T** operator&() { return &p_; }
    T* operator->() const { return p_; }
    operator T*() const { return p_; }
    explicit operator bool() const { return p_ != nullptr; }
private:
    T* p_ = nullptr;
};
