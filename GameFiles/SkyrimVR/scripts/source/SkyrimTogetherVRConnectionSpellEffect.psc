Scriptname SkyrimTogetherVRConnectionSpellEffect extends ActiveMagicEffect

Event OnEffectStart(Actor akTarget, Actor akCaster)
    SkyrimTogetherVRConnectionMenu.ToggleConfigured()
EndEvent
