# Window Input Delta

## Purpose

为桌面窗口提供独立于操作系统类型的键鼠输入，使调用方在每次更新中读取有序的按住状态和相对鼠标移动，并在窗口失去交互条件后可靠地停止响应。

## ADDED Requirements

### Requirement: Window scoped input state

Win64 Game SHALL 在窗口消息处理后为当前窗口生成一份当帧输入状态，包含 W/A/S/D 的按住状态、右键视角捕获状态及该帧可用的相对鼠标位移。长按状态跨帧保留，按键自动重复 MUST NOT 产生额外的移动，鼠标位移 SHALL 只消费一次。

#### Scenario: Hold and release a movement key

- **WHEN** 窗口有焦点且可用，按住 W 跨越多帧，随后松开
- **THEN** W 从按下所在帧起保持生效，松开所在帧停止；系统重复消息不改变移动速度

#### Scenario: Read ordered input once per frame

- **WHEN** 同一次消息处理内按下又松开 W，或捕获期间收到多条鼠标移动消息后进入无新输入的一帧
- **THEN** W 以当帧最终状态为准，已接收的相对位移当帧累计一次，后续无新移动的帧位移为零；其他窗口的输入不会修改该状态

### Requirement: Right button capture and cancellation

Win64 Game SHALL 在有焦点、未最小化且客户区尺寸有效的窗口中，按下右键并获得捕获后接收相对鼠标移动。正常松开右键 SHALL 停止捕获。失焦、最小化、客户区失效、系统取消、意外失去捕获或关闭窗口 SHALL 清除按住状态和未消费位移，并在持有捕获时释放它。恢复后 SHALL 等待新的按下事件。

#### Scenario: Look while captured and release outside the client area

- **WHEN** 在客户区内按住右键并移动鼠标，光标经过客户区边缘或屏幕边缘，并在窗口外松开右键
- **THEN** 捕获期间的相对移动仍能转动视角；松开之后的移动不再转动视角，正常结束捕获不影响仍按住的 WASD

#### Scenario: Cancel while keys and the right button are held

- **WHEN** 用户在转向和移动时切走焦点、最小化或系统夺走捕获，再回到窗口
- **THEN** 键盘移动和鼠标转向均停止，未消费位移被丢弃，仍按住按键产生的重复消息不会重启移动；重新按下后按正常规则响应

#### Scenario: Capture fails or the window closes

- **WHEN** 窗口未能获得右键捕获，或在捕获期间收到关闭请求
- **THEN** 捕获失败不启动转向；关闭路径释放输入状态，窗口仍可通过系统快捷键或关闭按钮正常退出
