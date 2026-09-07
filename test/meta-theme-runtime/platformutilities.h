#pragma once
class PlatformUtilities
{
  public:
    static PlatformUtilities *instance()
    {
      static PlatformUtilities platform;
      return &platform;
    }
    bool isSystemDarkTheme() const { return true; }
};
