/* Test fixture: HasMissingParent extends Stub, but gen_fixtures.py packs
 * only HasMissingParent.class into test.jar. Loading it must fail and name
 * "Stub" as the missing class. */
class Stub {
}

class HasMissingParent extends Stub {
}
