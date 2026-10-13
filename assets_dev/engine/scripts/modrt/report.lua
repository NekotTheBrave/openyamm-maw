-- Compatibility report: what loaded, what failed, and everything the mods reached for that the runtime does not
-- provide yet. Written to logs/mod_report.txt; this is the work list for the binding milestones.

local ModRT = ModRT
local modrt = modrt

local function sortedByCount(counts)
    local rows = {}

    for name, count in pairs(counts) do
        rows[#rows + 1] = {name = name, count = count}
    end

    table.sort(rows, function(left, right)
        if left.count == right.count then
            return left.name < right.name
        end

        return left.count > right.count
    end)

    return rows
end

local function sortedByName(counts)
    local rows = {}

    for name, count in pairs(counts) do
        rows[#rows + 1] = {name = name, count = count}
    end

    table.sort(rows, function(left, right) return left.name < right.name end)
    return rows
end

local function rootOf(path)
    return path:match("^[%w_]+") or path
end

function ModRT.buildReport()
    local out = {}

    local function line(text)
        out[#out + 1] = text or ""
    end

    local function section(title)
        line()
        line(("="):rep(100))
        line(title)
        line(("="):rep(100))
    end

    -- files
    local statusCounts = {ok = 0, error = 0, skipped = 0}

    for _, file in ipairs(ModRT.Loader.files) do
        statusCounts[file.status] = (statusCounts[file.status] or 0) + 1
    end

    line("OpenYAMM mod runtime report")
    line(("files: %d loaded, %d failed, %d skipped"):format(statusCounts.ok, statusCounts.error, statusCounts.skipped))
    line(("distinct errors: %d (total occurrences %d)"):format(#ModRT.errorOrder, ModRT.totalErrorCount))

    section("FILES THAT FAILED")

    for _, file in ipairs(ModRT.Loader.files) do
        if file.status == "error" then
            line(file.name)
            line("    " .. tostring(file.detail))
        end
    end

    section("FILES SKIPPED")

    for _, file in ipairs(ModRT.Loader.files) do
        if file.status == "skipped" then
            line(("%-60s %s"):format(file.name, tostring(file.detail)))
        end
    end

    section("FILES LOADED")

    for _, file in ipairs(ModRT.Loader.files) do
        if file.status == "ok" then
            line(file.name .. (file.detail and ("  (" .. file.detail .. ")") or ""))
        end
    end

    -- events
    section("EVENTS: handlers registered by mods, and whether the engine fires the event yet")
    local registered = ModRT.Events.registered
    local fired = ModRT.Events.fired
    local firedCount, unfiredCount = 0, 0

    for _, row in ipairs(sortedByCount(registered)) do
        local isFired = fired[row.name] ~= nil
        firedCount = firedCount + (isFired and 1 or 0)
        unfiredCount = unfiredCount + (isFired and 0 or 1)
        line(("%-34s handlers=%-4d %s"):format(row.name, row.count, isFired and "fired" or "NOT FIRED"))
    end

    line()
    line(("%d event names fired by the engine, %d not yet"):format(firedCount, unfiredCount))

    -- engine API
    section("ENGINE API reached through stand-ins (no binding yet), by root")
    local byRoot = {}
    local rootTotals = {}

    for path, count in pairs(ModRT.Stubs.accessed) do
        local root = rootOf(path)
        byRoot[root] = byRoot[root] or {}
        byRoot[root][path] = count
        rootTotals[root] = (rootTotals[root] or 0) + 1
    end

    for _, root in ipairs(sortedByCount(rootTotals)) do
        line()
        line(("--- %s: %d distinct paths"):format(root.name, root.count))

        for _, row in ipairs(sortedByName(byRoot[root.name])) do
            line(("    %-70s x%d"):format(row.name, row.count))
        end
    end

    -- undefined globals
    section("UNDEFINED GLOBALS read by mods (helpers the Merge base or MMExtension would have defined)")

    for _, row in ipairs(sortedByCount(ModRT.Stubs.undefinedGlobals)) do
        line(("%-44s x%d"):format(row.name, row.count))
    end

    -- files
    section("FILE ACCESS")

    for _, row in ipairs(sortedByName(ModRT.Env.fileAccess)) do
        line(("%-90s x%d"):format(row.name, row.count))
    end

    -- errors
    section("ERRORS, most frequent first")
    local errors = {}

    for _, entry in ipairs(ModRT.errorOrder) do
        errors[#errors + 1] = entry
    end

    table.sort(errors, function(left, right) return left.count > right.count end)

    for _, entry in ipairs(errors) do
        line(("x%-5d %s"):format(entry.count, entry.message))
        line("       first seen in: " .. tostring(entry.context))
    end

    section("HANDLERS SWITCHED OFF after repeated failures")

    for _, description in pairs(ModRT.disabledHandlers) do
        line(description)
    end

    local summary = ("%d files loaded, %d failed, %d skipped; %d distinct errors; %d events not fired yet"):format(
        statusCounts.ok, statusCounts.error, statusCounts.skipped, #ModRT.errorOrder, unfiredCount)
    return table.concat(out, "\n") .. "\n", summary
end

function ModRT.writeReport()
    local text, summary = ModRT.buildReport()
    local path = modrt.writeReport("mod_report.txt", text)
    return summary .. (path and ("\nreport: " .. path) or "\nreport could not be written")
end
