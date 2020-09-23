/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "HTMLDivElement.h"
#include "nsGenericHTMLElement.h"
#include "nsStyleConsts.h"
#include "nsMappedAttributes.h"
#include "mozilla/dom/HTMLDivElementBinding.h"

NS_IMPL_NS_NEW_HTML_ELEMENT(Div)

namespace mozilla {
namespace dom {

HTMLDivElement::~HTMLDivElement()
{
}

NS_IMPL_ELEMENT_CLONE(HTMLDivElement)

JSObject*
HTMLDivElement::WrapNode(JSContext *aCx, JS::Handle<JSObject*> aGivenProto)
{
  return dom::HTMLDivElementBinding::Wrap(aCx, this, aGivenProto);
}

bool
HTMLDivElement::ParseAttribute(int32_t aNamespaceID,
                               nsIAtom* aAttribute,
                               const nsAString& aValue,
                               nsAttrValue& aResult)
{
  if (aNamespaceID == kNameSpaceID_None) {
    if (mNodeInfo->Equals(nsGkAtoms::div) &&
        aAttribute == nsGkAtoms::align) {
      return ParseDivAlignValue(aValue, aResult);
    }
  }

  return nsGenericHTMLElement::ParseAttribute(aNamespaceID, aAttribute, aValue,
                                              aResult);
}

void
HTMLDivElement::MapAttributesIntoRule(const nsMappedAttributes* aAttributes,
                                      nsRuleData* aData)
{
  nsGenericHTMLElement::MapDivAlignAttributeInto(aAttributes, aData);
  nsGenericHTMLElement::MapCommonAttributesInto(aAttributes, aData);
}

NS_IMETHODIMP_(bool)
HTMLDivElement::IsAttributeMapped(const nsIAtom* aAttribute) const
{
  if (mNodeInfo->Equals(nsGkAtoms::div)) {
    static const MappedAttributeEntry* const map[] = {
      sDivAlignAttributeMap,
      sCommonAttributeMap
    };
    return FindAttributeDependence(aAttribute, map);
  }

  return nsGenericHTMLElement::IsAttributeMapped(aAttribute);
}

nsMapRuleToAttributesFunc
HTMLDivElement::GetAttributeMappingFunction() const
{
  if (mNodeInfo->Equals(nsGkAtoms::div)) {
    return &MapAttributesIntoRule;
  }
  return nsGenericHTMLElement::GetAttributeMappingFunction();
}

} // namespace dom
} // namespace mozilla
