-- Animate.lua – direct R6 Motor6D animation (no KeyframeSequence / HTTP).
-- X axis on shoulders = arm swings forward/backward from hanging position.
-- Z axis on shoulders = arm spreads outward/upward from hanging position.

print("[Animate] starting...")

local ok, err = pcall(function()

local RunService = game:GetService("RunService")
local figure     = script.Parent
local humanoid   = figure:WaitForChild("Humanoid", 10)
local torso      = figure:WaitForChild("Torso",    10)

if not humanoid then error("Humanoid not found") end
if not torso    then error("Torso not found")    end

local CF  = CFrame.new
local A   = CFrame.Angles
local sin = math.sin
local abs = math.abs

-- ── Grab Motor6Ds from Torso ───────────────────────────────────────────────
local lShoulder = torso:WaitForChild("Left Shoulder",  5)
local rShoulder = torso:WaitForChild("Right Shoulder", 5)
local lHip      = torso:WaitForChild("Left Hip",       5)
local rHip      = torso:WaitForChild("Right Hip",      5)
local neck      = torso:FindFirstChild("Neck")

if not (lShoulder and rShoulder and lHip and rHip) then
    error("Motor6Ds missing from Torso")
end

-- ── Snapshot default C0 (character's rest pose) ───────────────────────────
local def = {
    lShoulder = lShoulder.C0,
    rShoulder = rShoulder.C0,
    lHip      = lHip.C0,
    rHip      = rHip.C0,
    neck      = neck and neck.C0 or CF(0, 1, 0),
}

print("[Animate] motors found, defaults snapshotted")

-- ── Tool detection ─────────────────────────────────────────────────────────
local function getEquippedTool()
    for _, child in ipairs(figure:GetChildren()) do
        if child:IsA("Tool") then return child end
    end
    return nil
end

-- ── Tuning ─────────────────────────────────────────────────────────────────
local WALK_CYCLE = 0.55
local WALK_LEG   = 0.45
local WALK_ARM   = 0.28
local FREQ       = 2 * math.pi / WALK_CYCLE

local t = 0

-- ── Helpers ────────────────────────────────────────────────────────────────
local function resetMotors()
    lShoulder.C0 = def.lShoulder
    rShoulder.C0 = def.rShoulder
    lHip.C0      = def.lHip
    rHip.C0      = def.rHip
    if neck then neck.C0 = def.neck end
end

-- ── Heartbeat loop ─────────────────────────────────────────────────────────
RunService.Heartbeat:connect(function(dt)
    local ok2, e2 = pcall(function()
        t = t + dt

        local vel       = torso.Velocity
        local horzSpeed = math.sqrt(vel.X * vel.X + vel.Z * vel.Z)
        local state     = tostring(humanoid:GetState())

        -- ── JUMP ──────────────────────────────────────────────────────────
        if state:find("Jumping") then
            -- Arms flung wide and slightly back — dramatic jump pose
            lShoulder.C0 = def.lShoulder * A(-0.40, 0, -1.0)
            rShoulder.C0 = def.rShoulder * A(-0.40, 0,  1.0)
            lHip.C0      = def.lHip
            rHip.C0      = def.rHip
            if neck then neck.C0 = def.neck * A(-0.15, 0, 0) end

        -- ── FALL ──────────────────────────────────────────────────────────
        elseif state:find("Freefall") or state:find("FallingDown") then
            lShoulder.C0 = def.lShoulder * A(0, 0, -0.40)
            rShoulder.C0 = def.rShoulder * A(0, 0,  0.40)
            lHip.C0      = def.lHip
            rHip.C0      = def.rHip
            if neck then neck.C0 = def.neck * A(0.10, 0, 0) end

        -- ── SWIM ──────────────────────────────────────────────────────────
        elseif state:find("Swimming") then
            local s = sin(t * FREQ)
            -- Both arms sweep forward/backward together (breaststroke)
            lShoulder.C0 = def.lShoulder * A(s * WALK_ARM * 1.3, 0, 0)
            rShoulder.C0 = def.rShoulder * A(s * WALK_ARM * 1.3, 0, 0)
            lHip.C0      = def.lHip      * A(-s * WALK_LEG, 0, 0)
            rHip.C0      = def.rHip      * A(-s * WALK_LEG, 0, 0)
            if neck then neck.C0 = def.neck * A(0.20, 0, 0) end

        -- ── WALK ──────────────────────────────────────────────────────────
        elseif horzSpeed > 0.5 then
            local s    = sin(t * FREQ)
            local tool = getEquippedTool()
            -- Arms sweep forward/backward on X axis (same as swim), both in sync
            if tool then
                rShoulder.C0 = def.rShoulder * A(-1.2, 0, 0)
                lShoulder.C0 = def.lShoulder * A(s * WALK_ARM, 0, 0)
            else
                lShoulder.C0 = def.lShoulder * A(s * WALK_ARM, 0, 0)
                rShoulder.C0 = def.rShoulder * A(s * WALK_ARM, 0, 0)
            end
            lHip.C0 = def.lHip * A(-s * WALK_LEG, 0, 0)
            rHip.C0 = def.rHip * A(-s * WALK_LEG, 0, 0)
            if neck then neck.C0 = def.neck * A(-0.05 + abs(s) * 0.02, 0, 0) end

        -- ── IDLE ──────────────────────────────────────────────────────────
        else
            local breathe = sin(t * 1.2) * 0.013
            local tool    = getEquippedTool()
            if tool then
                -- Arm raised forward to proper carry height (~75 degrees)
                rShoulder.C0 = def.rShoulder * A(-1.3, 0, 0)
                lShoulder.C0 = def.lShoulder
            else
                lShoulder.C0 = def.lShoulder
                rShoulder.C0 = def.rShoulder
            end
            lHip.C0 = def.lHip
            rHip.C0 = def.rHip
            if neck then neck.C0 = def.neck * A(breathe, 0, 0) end
        end
    end)
    if not ok2 then
        print("[Animate] heartbeat error:", tostring(e2))
    end
end)

print("[Animate] init complete")

end)

if not ok then
    print("[Animate] FATAL:", tostring(err))
end
