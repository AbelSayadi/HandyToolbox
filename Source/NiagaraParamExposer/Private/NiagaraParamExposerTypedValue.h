#pragma once
#include "NiagaraTypes.h"

namespace NiagaraParamExposerTypedValue
{
// Validate before creating graph nodes; never silently convert an invalid value to zero.
inline bool Parse(const FString& TypeName, const FString& Text, FNiagaraVariable& Out, FString& PinDefault)
{
 FString Type = TypeName.ToLower();
 if (Type == TEXT("linearcolor")) Type = TEXT("color");
 if (Type == TEXT("vector3") || Type == TEXT("vec3")) Type = TEXT("vector");
 if (Type == TEXT("vec2")) Type = TEXT("vector2d");
 if (Type == TEXT("int32")) Type = TEXT("int");
 const FString Value = Text.TrimStartAndEnd();
 auto Store = [&](const FNiagaraTypeDefinition& Def, const auto& Data, const FString& Default)
 {
  Out = FNiagaraVariable(Def, NAME_None); Out.SetValue(Data); PinDefault = Default; return true;
 };
 if (Type == TEXT("float"))
 {
  float V; if (!Value.IsNumeric() || !LexTryParseString(V, *Value) || !FMath::IsFinite(V)) return false;
  return Store(FNiagaraTypeDefinition::GetFloatDef(), V, FString::SanitizeFloat(V));
 }
 if (Type == TEXT("int"))
 {
  int32 V; if (Value.IsEmpty()) return false;
  for (int32 I=0; I<Value.Len(); ++I) if (!FChar::IsDigit(Value[I]) && !(I==0 && (Value[I]=='-' || Value[I]=='+'))) return false;
  if (!LexTryParseString(V, *Value)) return false;
  return Store(FNiagaraTypeDefinition::GetIntDef(), V, FString::FromInt(V));
 }
 if (Type == TEXT("bool"))
 {
  if (Value != TEXT("0") && Value != TEXT("1") && !Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) && !Value.Equals(TEXT("false"), ESearchCase::IgnoreCase)) return false;
  const bool V = Value == TEXT("1") || Value.Equals(TEXT("true"), ESearchCase::IgnoreCase);
  return Store(FNiagaraTypeDefinition::GetBoolDef(), FNiagaraBool(V), V ? TEXT("true") : TEXT("false"));
 }
 const int32 Count = Type == TEXT("vector2d") ? 2 : Type == TEXT("vector") ? 3 : Type == TEXT("color") ? 4 : 0;
 if (!Count) return false;
 TArray<FString> Parts; Value.ParseIntoArray(Parts, TEXT(","), false);
 if (Parts.Num()!=Count) return false;
 float V[4] = {};
 for (int32 I=0; I<Count; ++I)
 {
  Parts[I].TrimStartAndEndInline();
  if (!Parts[I].IsNumeric() || !LexTryParseString(V[I], *Parts[I]) || !FMath::IsFinite(V[I])) return false;
 }
 if (Count==2) { FVector2f Data(V[0],V[1]); return Store(FNiagaraTypeDefinition::GetVec2Def(), Data, Data.ToString()); }
 if (Count==3) { FVector3f Data(V[0],V[1],V[2]); return Store(FNiagaraTypeDefinition::GetVec3Def(), Data, Data.ToString()); }
 FLinearColor Data(V[0],V[1],V[2],V[3]); return Store(FNiagaraTypeDefinition::GetColorDef(), Data, Data.ToString());
}
}
