#pragma once

/** @brief Configures the platform and owns the application composition root. */
class ApplicationBootstrap final
{
public:
    static int run(int argc, char *argv[]);
};
