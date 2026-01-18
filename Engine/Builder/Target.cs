using System;
using Sharpmake;

namespace Limitless
{
    [Fragment, Flags]
    public enum TargetType
    {
        Game = 1 << 0,    // 对应 Monolithic：所有模块打成一个包
        Editor = 1 << 1,  // 对应 Modular：模块全是 DLL，方便热更和开发
    }

    public class TargetRule : Sharpmake.Target
    {
        public TargetType TargetType;

        public TargetRule() { }

        public TargetRule(
            Platform platform, 
            DevEnv devEnv, 
            Optimization optimization,
            TargetType buildType // <--- 新增维度
        )
            : base(platform, devEnv, optimization)
        {
            TargetType = buildType;
        }
    }
}